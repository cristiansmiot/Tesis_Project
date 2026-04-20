#include "task_measurement.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "ade9153a_init.h"
#include "active_power.h"
#include "apparent_power.h"
#include "current.h"
#include "meter_data.h"
#include "meter_config.h"
#include "node_health.h"
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "pq_monitor.h"
#include "power_factor.h"
#include "reactive_power.h"
#include "rtos_app_config.h"
#include "task_calibration.h"
#include "task_manager.h"
#include "temperature.h"
#include "voltage.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "fault_handler.h"
#include "logger.h"

static const char *TAG = "task_measurement";
static uint32_t s_last_vrms_raw;
static bool s_have_vrms_raw;
static bool s_ac_present_latched;
static uint8_t s_ac_on_count;
static uint8_t s_ac_lost_count;
static float s_energy_wh_soft;
static float s_energy_wh_total_nvs;
static float s_energy_wh_total_saved;
static int32_t s_last_energy_hi_raw;
static int64_t s_energy_last_save_us;
static bool s_energy_nvs_ready;
static nvs_handle_t s_energy_nvs;
static float s_last_pf_valid;
static bool s_have_pf_valid;
static uint8_t s_pf_hold_samples;
static uint8_t s_metrology_stall_count;
static uint8_t s_ade_invalid_count;
static uint8_t s_ade_recovery_fail_streak;
static int64_t s_last_recovery_us;
static bool s_ade_ready_confirmed;
static bool s_ade_link_previously_valid;

static bool task_measurement_raw_vrms_is_valid(uint32_t raw_vrms)
{
    return (raw_vrms != 0U) && (raw_vrms != 0xFFFFFFFFU);
}

static bool task_measurement_aperiod_raw_is_valid(uint32_t raw_aperiod)
{
    return (raw_aperiod != 0U) && (raw_aperiod != 0xFFFFFFFFU) && (raw_aperiod < 0x00FFFFFFU);
}

static const char *task_measurement_ac_status_text(bool ade_ready, bool ac_present)
{
    if (!ade_ready) {
        return "ERROR";
    }
    return ac_present ? "PRESENT" : "LOST";
}

static void task_measurement_zero_electrical_snapshot(MeterData_t *snap, bool clear_temperature)
{
    if (snap == NULL) {
        return;
    }

    snap->vrms = 0.0f;
    snap->vpeak = 0.0f;
    snap->frequency = 0.0f;
    snap->irms_a = 0.0f;
    snap->irms_b = 0.0f;
    snap->active_power_w = 0.0f;
    snap->reactive_power_var = 0.0f;
    snap->apparent_power_va = 0.0f;
    snap->power_factor = 0.0f;
    snap->energy_hw_wh = 0.0f;
    snap->energy_varh = 0.0f;
    snap->energy_vah = 0.0f;
    if (clear_temperature) {
        snap->temperature = 0.0f;
        snap->temperature_valid = 0U;
    }
}

static bool task_measurement_snapshot_is_plausible(const MeterData_t *snap)
{
    if (snap == NULL) {
        return false;
    }
    if ((snap->vrms < 0.0f) || (snap->vrms > METER_VRMS_SANITY_MAX_V)) {
        return false;
    }
    if ((snap->frequency < 0.0f) || (snap->frequency > (METER_AC_PRESENT_FREQ_MAX_HZ + 5.0f))) {
        return false;
    }
    if (fabsf(snap->irms_a) > (METER_IMAX_A * 2.0f)) {
        return false;
    }
    if (fabsf(snap->irms_b) > (METER_IMAX_A * 2.0f)) {
        return false;
    }
    if (fabsf(snap->active_power_w) > METER_ACTIVE_POWER_SANITY_MAX_W) {
        return false;
    }
    if (fabsf(snap->reactive_power_var) > METER_REACTIVE_POWER_SANITY_MAX_VAR) {
        return false;
    }
    if ((snap->apparent_power_va < 0.0f) || (snap->apparent_power_va > METER_APPARENT_POWER_SANITY_MAX_VA)) {
        return false;
    }
    if ((snap->apparent_power_va > METER_NO_LOAD_ACTIVE_POWER_W_MAX) &&
        (fabsf(snap->active_power_w) > (snap->apparent_power_va + METER_NO_LOAD_ACTIVE_POWER_W_MAX))) {
        return false;
    }
    if ((snap->temperature_valid != 0U) &&
        ((snap->temperature < METER_TEMP_SANITY_MIN_C) || (snap->temperature > METER_TEMP_SANITY_MAX_C))) {
        return false;
    }
    if ((snap->power_factor < -1.05f) || (snap->power_factor > 1.05f)) {
        return false;
    }
    return true;
}

static bool task_measurement_reconfirm_ade_ready(void)
{
    ade9153a_diag_t diag = {0};
    const esp_err_t err = ade9153a_get_diag(&diag);
    if ((err == ESP_OK) && diag.ready) {
        if (!s_ade_ready_confirmed) {
            LOG_WARN(TAG, "ADE probe revalidated - measurements trusted again");
        }
        s_ade_ready_confirmed = true;
        return true;
    }

    s_ade_ready_confirmed = false;
    return false;
}

static void task_measurement_reset_ade_observers(void)
{
    s_have_vrms_raw = false;
    s_last_vrms_raw = 0U;
    s_ac_present_latched = false;
    s_ac_on_count = 0U;
    s_ac_lost_count = 0U;
    s_have_pf_valid = false;
    s_pf_hold_samples = 0U;
    s_metrology_stall_count = 0U;
    s_ade_invalid_count = 0U;
    s_ade_link_previously_valid = false;
}

static void task_measurement_try_ade_recovery(const char *reason, int64_t now_us, bool force_spi_reinit)
{
    // Cuando ESCALATE_AFTER=0 se fuerza SPI_REINIT desde el primer intento.
    // Se usa #if para evitar la comparacion unsigned>=0 que dispara -Wtype-limits.
#if (METER_ADE_RECOVERY_ESCALATE_AFTER == 0U)
    const bool use_spi_reinit = true;
    (void)force_spi_reinit;
#else
    const bool use_spi_reinit = force_spi_reinit ||
                                (s_ade_recovery_fail_streak >= METER_ADE_RECOVERY_ESCALATE_AFTER);
#endif
    const ade9153a_recovery_action_t requested_action = use_spi_reinit ?
        ADE9153A_RECOVERY_ACTION_SPI_REINIT :
        ADE9153A_RECOVERY_ACTION_SOFT_INIT;
    ade9153a_recovery_action_t performed_action = requested_action;

    LOG_WARN(TAG,
             "ADE recovery requested: reason=%s action=%u invalid=%u stall=%u",
             reason,
             (unsigned)requested_action,
             (unsigned)s_ade_invalid_count,
             (unsigned)s_metrology_stall_count);

    esp_err_t recovery_err = ade9153a_recover(requested_action);
    // No escalar inmediatamente de SOFT_INIT a SPI_REINIT en el mismo intento.
    // Dos resets consecutivos sin pausa empeoran la situacion cuando el ADE
    // tiene Vcc inestable. Se deja que el proximo ciclo de recovery escale
    // con el cooldown apropiado.

    if (recovery_err != ESP_OK) {
        LOG_WARN(TAG, "ADE recovery failed: %s (streak=%u)",
                 esp_err_to_name(recovery_err),
                 (unsigned)(s_ade_recovery_fail_streak + 1U));
        fault_set(FAULT_SPI, reason);
        s_ade_ready_confirmed = false;
        if (s_ade_recovery_fail_streak < UINT8_MAX) {
            ++s_ade_recovery_fail_streak;
        }
        if (s_ade_recovery_fail_streak == METER_ADE_RECOVERY_MAX_FAST_RETRIES) {
            LOG_WARN(TAG,
                     "ADE %u fallos consecutivos - backoff a %ums entre intentos",
                     (unsigned)s_ade_recovery_fail_streak,
                     (unsigned)METER_ADE_RECOVERY_SLOW_COOLDOWN_MS);
        }
    } else {
        LOG_WARN(TAG, "ADE recovery completed action=%u", (unsigned)performed_action);
        s_ade_recovery_fail_streak = 0U;
        s_ade_ready_confirmed = true;
        node_health_ade_recovery_success_inc();
        fault_clear_last();
    }

    s_last_recovery_us = now_us;
    task_measurement_reset_ade_observers();
}

static float task_measurement_pf_clamp(float pf)
{
    if (pf > 1.0f) {
        return 1.0f;
    }
    if (pf < -1.0f) {
        return -1.0f;
    }
    return pf;
}

static bool task_measurement_pf_fallback_from_vi(const MeterData_t *snap, float *pf_out)
{
    if ((snap == NULL) || (pf_out == NULL)) {
        return false;
    }

    const float abs_p = fabsf(snap->active_power_w);
    const float abs_i = fabsf(snap->irms_a);
    const float abs_v = fabsf(snap->vrms);
    const float apparent_va = abs_v * abs_i;
    if ((abs_p < METER_PF_FALLBACK_MIN_ACTIVE_POWER_W) ||
        (apparent_va < METER_PF_FALLBACK_MIN_APPARENT_POWER_VA) ||
        (abs_i <= 0.0f) ||
        (abs_v <= 0.0f)) {
        return false;
    }

    *pf_out = task_measurement_pf_clamp(snap->active_power_w / (snap->vrms * snap->irms_a));
    return true;
}

/**
 * @brief Evalua presencia AC candidata con histeresis por VRMS crudo y guardas de frecuencia.
 * @param snap Snapshot de mediciones del ciclo actual.
 * @return true si AC parece presente en este ciclo.
 */
static bool task_measurement_is_ac_present_candidate(const MeterData_t *snap)
{
    if (snap == NULL) {
        return false;
    }

    const bool freq_valid =
        (snap->frequency >= METER_AC_PRESENT_FREQ_MIN_HZ) &&
        (snap->frequency <= METER_AC_PRESENT_FREQ_MAX_HZ);
    const bool vrms_present = (snap->vrms >= METER_AC_PRESENT_VRMS_MIN_V) &&
                              (snap->vrms <= METER_VRMS_SANITY_MAX_V);
    const bool vrms_lost = (snap->vrms <= METER_AC_LOST_VRMS_MAX_V);
    const bool raw_present =
        s_have_vrms_raw && (s_last_vrms_raw >= METER_AC_PRESENT_VRMS_RAW_ON_MIN);
    const bool raw_lost =
        s_have_vrms_raw && (s_last_vrms_raw <= METER_AC_PRESENT_VRMS_RAW_LOST_MAX);

    // Si ya estamos en PRESENT, permitir caida a LOST si no hay RAW valido y
    // los indicadores electricos ya no soportan presencia.
    if (s_ac_present_latched) {
        const bool electrical_lost = vrms_lost || !freq_valid;
        if (raw_lost || (!s_have_vrms_raw && electrical_lost)) {
            return false;
        }
        return true;
    }

    // Si estamos en LOST, exigir evidencia clara de regreso.
    return (raw_present || vrms_present) && freq_valid;
}

/**
 * @brief Detecta metrologia congelada con AC presumiblemente presente.
 * @param snap Snapshot de mediciones del ciclo actual.
 * @return true si Vrms/F estan invalidos sin RAW reciente pero con ultimo RAW alto.
 */
static bool task_measurement_is_metrology_stalled(const MeterData_t *snap)
{
    if (snap == NULL) {
        return false;
    }

    const bool vrms_lost = (snap->vrms <= METER_AC_LOST_VRMS_MAX_V);
    const bool freq_invalid =
        (snap->frequency < METER_AC_PRESENT_FREQ_MIN_HZ) ||
        (snap->frequency > METER_AC_PRESENT_FREQ_MAX_HZ);
    const bool stale_raw_high =
        (!s_have_vrms_raw) && (s_last_vrms_raw >= METER_AC_PRESENT_VRMS_RAW_ON_MIN);

    return stale_raw_high && vrms_lost && freq_invalid;
}

/**
 * @brief Inicializa persistencia NVS de energia acumulada.
 * @return ESP_OK si la capa NVS queda operativa.
 */
static esp_err_t task_measurement_energy_nvs_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_INVALID_STATE) {
        err = ESP_OK;
    }
    if ((err == ESP_ERR_NVS_NO_FREE_PAGES) || (err == ESP_ERR_NVS_NEW_VERSION_FOUND)) {
        LOG_WARN(TAG, "nvs_flash requires erase (%s)", esp_err_to_name(err));
        err = nvs_flash_erase();
        if (err != ESP_OK) {
            LOG_WARN(TAG, "nvs_flash_erase failed: %s", esp_err_to_name(err));
            return err;
        }
        err = nvs_flash_init();
        if (err == ESP_ERR_NVS_INVALID_STATE) {
            err = ESP_OK;
        }
    }
    if (err != ESP_OK) {
        LOG_WARN(TAG, "nvs_flash_init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_open(METER_ENERGY_NVS_NAMESPACE, NVS_READWRITE, &s_energy_nvs);
    if (err != ESP_OK) {
        LOG_WARN(TAG, "nvs_open(%s) failed: %s", METER_ENERGY_NVS_NAMESPACE, esp_err_to_name(err));
        return err;
    }

    double total_wh = 0.0;
    size_t blob_size = sizeof(total_wh);
    err = nvs_get_blob(s_energy_nvs, METER_ENERGY_NVS_KEY_TOTAL_WH, &total_wh, &blob_size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        total_wh = 0.0;
        err = ESP_OK;
    } else if ((err == ESP_OK) && (blob_size != sizeof(total_wh))) {
        LOG_WARN(TAG, "nvs key %s has invalid size %u", METER_ENERGY_NVS_KEY_TOTAL_WH, (unsigned)blob_size);
        total_wh = 0.0;
        err = ESP_OK;
    }
    if (err != ESP_OK) {
        LOG_WARN(TAG, "nvs_get_blob(%s) failed: %s", METER_ENERGY_NVS_KEY_TOTAL_WH, esp_err_to_name(err));
        nvs_close(s_energy_nvs);
        s_energy_nvs = 0;
        return err;
    }

    s_energy_wh_total_nvs = (float)total_wh;
    if (s_energy_wh_total_nvs < 0.0f) {
        s_energy_wh_total_nvs = 0.0f;
    }
    s_energy_wh_total_saved = s_energy_wh_total_nvs;
    s_energy_last_save_us = esp_timer_get_time();
    s_energy_nvs_ready = true;

    LOG_INFO(TAG, "energy NVS loaded: %.4fWh", (double)s_energy_wh_total_nvs);
    return ESP_OK;
}

/**
 * @brief Guarda checkpoint de energia total en NVS con throttling.
 * @param force true para forzar guardado inmediato.
 */
static void task_measurement_energy_nvs_try_save(bool force)
{
    if (!s_energy_nvs_ready) {
        return;
    }

    const int64_t now_us = esp_timer_get_time();
    const int64_t save_period_us = ((int64_t)METER_ENERGY_NVS_SAVE_PERIOD_MS) * 1000LL;
    const bool period_elapsed = ((now_us - s_energy_last_save_us) >= save_period_us);
    const float delta_wh = fabsf(s_energy_wh_total_nvs - s_energy_wh_total_saved);

    if (!force) {
        if (!period_elapsed) {
            return;
        }
        if (delta_wh < METER_ENERGY_NVS_SAVE_MIN_DELTA_WH) {
            return;
        }
    }

    double total_wh = (double)s_energy_wh_total_nvs;
    esp_err_t err = nvs_set_blob(s_energy_nvs, METER_ENERGY_NVS_KEY_TOTAL_WH, &total_wh, sizeof(total_wh));
    if (err == ESP_OK) {
        err = nvs_commit(s_energy_nvs);
    }
    if (err != ESP_OK) {
        LOG_WARN(TAG, "nvs save failed: %s", esp_err_to_name(err));
        return;
    }

    s_energy_wh_total_saved = s_energy_wh_total_nvs;
    s_energy_last_save_us = now_us;
}

/**
 * @brief Tarea periodica de adquisicion de metrologia.
 * @param pvParameters Parametros de tarea (no usado).
 * @return void
 */
void task_measurement(void *pvParameters)
{
    (void)pvParameters;

    TickType_t xLastWakeTime = xTaskGetTickCount();
    const uint32_t cycle_ms = METER_MEASUREMENT_PERIOD_MS;
    uint32_t log_divider = (cycle_ms > 0U) ? (1000U / cycle_ms) : 1U;
    if (log_divider == 0U) {
        log_divider = 1U;
    }
    uint32_t cycle_counter = 0U;
    bool prev_ac_present = false;

    s_last_vrms_raw = 0U;
    s_have_vrms_raw = false;
    s_ac_present_latched = false;
    s_ac_on_count = 0U;
    s_ac_lost_count = 0U;
    s_energy_wh_soft = 0.0f;
    s_energy_wh_total_nvs = 0.0f;
    s_energy_wh_total_saved = 0.0f;
    s_last_energy_hi_raw = 0;
    s_energy_last_save_us = 0;
    s_energy_nvs_ready = false;
    s_energy_nvs = 0;
    s_last_pf_valid = 0.0f;
    s_have_pf_valid = false;
    s_pf_hold_samples = 0U;
    s_metrology_stall_count = 0U;
    s_ade_invalid_count = 0U;
    s_ade_recovery_fail_streak = 0U;
    s_last_recovery_us = -((int64_t)METER_ADE_RECOVERY_COOLDOWN_MS) * 1000LL;
    s_ade_ready_confirmed = false;

    if (task_measurement_energy_nvs_init() != ESP_OK) {
        LOG_WARN(TAG, "energy NVS unavailable - using volatile runtime total");
    }

    ade9153a_diag_t boot_diag = {0};
    s_ade_ready_confirmed = (ade9153a_get_diag(&boot_diag) == ESP_OK) && boot_diag.ready;
    if (!s_ade_ready_confirmed) {
        LOG_WARN(TAG, "ADE not ready at task start - measurements masked until recovery succeeds");
    }

    for (;;) {
        // Regla TR-1: primer statement del loop.
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(cycle_ms));

        MeterData_t snap = {0};
        snap.vrms = voltage_get_vrms();
        snap.vpeak = voltage_get_vpeak();
        snap.frequency = voltage_get_frequency();
        snap.irms_a = current_get_irms_a();
        snap.irms_b = current_get_irms_b();
        snap.active_power_w = active_power_get_watts();
        snap.energy_hw_wh = active_power_get_wh();
        s_last_energy_hi_raw = active_power_get_wh_raw_hi();
        snap.reactive_power_var = reactive_power_get_var();
        snap.energy_varh = reactive_power_get_varh();
        snap.apparent_power_va = apparent_power_get_va();
        snap.energy_vah = apparent_power_get_vah();
        snap.power_factor = power_factor_get();
        snap.temperature = 0.0f;
        snap.temperature_valid = 0U;
        if (temperature_read_internal(&snap.temperature) == ESP_OK) {
            snap.temperature_valid = 1U;
        }
        snap.timestamp_us = esp_timer_get_time();
        snap.pq_status = pq_monitor_get_flags();
        snap.calibration_status = task_calibration_get_status();

        uint32_t vrms_raw = 0U;
        bool vrms_raw_valid_now = false;
        if (voltage_get_vrms_raw(&vrms_raw) == ESP_OK) {
            vrms_raw_valid_now = task_measurement_raw_vrms_is_valid(vrms_raw);
            if (vrms_raw_valid_now) {
                s_last_vrms_raw = vrms_raw;
                s_have_vrms_raw = true;
            } else {
                s_have_vrms_raw = false;
            }
        } else {
            s_have_vrms_raw = false;
        }

        uint32_t aperiod_raw = 0U;
        bool aperiod_raw_ok = (voltage_get_aperiod_raw(&aperiod_raw) == ESP_OK);
        const bool aperiod_raw_valid = aperiod_raw_ok && task_measurement_aperiod_raw_is_valid(aperiod_raw);
        const bool snapshot_implausible = !task_measurement_snapshot_is_plausible(&snap);
        if (snapshot_implausible) {
            s_ade_ready_confirmed = false;
        }
        if (!s_ade_ready_confirmed && (vrms_raw_valid_now || aperiod_raw_valid)) {
            (void)task_measurement_reconfirm_ade_ready();
        }
        const bool ade_link_invalid = ((!vrms_raw_valid_now) && (!aperiod_raw_valid)) ||
                                      snapshot_implausible ||
                                      !s_ade_ready_confirmed;
        const char *ade_invalid_reason = "ADE raw probe invalid";
        if (snapshot_implausible) {
            ade_invalid_reason = "ADE snapshot implausible";
        } else if (!s_ade_ready_confirmed) {
            ade_invalid_reason = "ADE not revalidated";
        }
        if (ade_link_invalid) {
            if (s_ade_invalid_count < UINT8_MAX) {
                ++s_ade_invalid_count;
            }
        } else {
            s_ade_invalid_count = 0U;
        }

        if (s_ade_link_previously_valid && ade_link_invalid) {
            node_health_ade_loss_inc();
        }
        s_ade_link_previously_valid = !ade_link_invalid;

        if (task_measurement_is_metrology_stalled(&snap)) {
            if (s_metrology_stall_count < UINT8_MAX) {
                ++s_metrology_stall_count;
            }
        } else {
            s_metrology_stall_count = 0U;
        }

        // Backoff: tras muchos fallos consecutivos, espaciar los intentos
        // para no martillar el bus SPI y dar tiempo a que Vcc se estabilice.
        const int64_t base_cooldown_us =
            ((int64_t)METER_ADE_RECOVERY_COOLDOWN_MS) * 1000LL;
        const int64_t slow_cooldown_us =
            ((int64_t)METER_ADE_RECOVERY_SLOW_COOLDOWN_MS) * 1000LL;
        const int64_t effective_cooldown_us =
            (s_ade_recovery_fail_streak >= METER_ADE_RECOVERY_MAX_FAST_RETRIES)
                ? slow_cooldown_us : base_cooldown_us;
        const bool cooldown_elapsed =
            ((snap.timestamp_us - s_last_recovery_us) >= effective_cooldown_us);

        if (cooldown_elapsed &&
            ((s_ade_invalid_count >= METER_ADE_INVALID_RECOVERY_SAMPLES) ||
             (s_metrology_stall_count >= METER_AC_STALL_RECOVERY_SAMPLES))) {
            const char *trigger_reason =
                (s_ade_invalid_count >= METER_ADE_INVALID_RECOVERY_SAMPLES)
                    ? ade_invalid_reason
                    : "ADE metrology stalled";

            // Antes de intentar recovery, verificar si el bus SPI realmente
            // esta muerto. Si el chip responde con ID valido y RUN activo,
            // las lecturas invalidas son por ausencia de AC o DSP en settle
            // tras un recovery reciente. Esto rompe el bucle infinito de
            // recovery cuando el ADE pierde alimentacion y regresa sin AC.
            ade9153a_diag_t pre_diag = {0};
            const esp_err_t probe_err = ade9153a_get_diag(&pre_diag);

            if ((probe_err == ESP_OK) && pre_diag.ready) {
                // Bus vivo - chip responde con ID y RUN validos.
                // No ejecutar recovery; resetear contadores y cooldown.
                LOG_INFO(TAG,
                         "ADE bus alive (probe OK) - skip recovery (was: %s inv=%u stall=%u)",
                         trigger_reason,
                         (unsigned)s_ade_invalid_count,
                         (unsigned)s_metrology_stall_count);
                s_ade_invalid_count = 0U;
                s_metrology_stall_count = 0U;
                s_ade_ready_confirmed = true;
                s_last_recovery_us = snap.timestamp_us;
            } else {
                // Bus muerto o sin respuesta - proceder con recovery real.
                task_measurement_try_ade_recovery(trigger_reason, snap.timestamp_us, false);
            }
        }

        if (snapshot_implausible || !s_ade_ready_confirmed) {
            task_measurement_zero_electrical_snapshot(&snap, true);
        }

        const bool ac_candidate = s_ade_ready_confirmed && task_measurement_is_ac_present_candidate(&snap);
        if (ac_candidate) {
            if (s_ac_on_count < UINT8_MAX) {
                ++s_ac_on_count;
            }
            s_ac_lost_count = 0U;
        } else {
            if (s_ac_lost_count < UINT8_MAX) {
                ++s_ac_lost_count;
            }
            s_ac_on_count = 0U;
        }

        if (!s_ac_present_latched && (s_ac_on_count >= METER_AC_DEBOUNCE_ON_SAMPLES)) {
            s_ac_present_latched = true;
        } else if (s_ac_present_latched && (s_ac_lost_count >= METER_AC_DEBOUNCE_LOST_SAMPLES)) {
            s_ac_present_latched = false;
        }

        const bool ac_present = s_ac_present_latched;
        const bool ac_changed = (ac_present != prev_ac_present);

        if (ac_present) {
            const bool no_load_current = (fabsf(snap.irms_a) <= METER_NO_LOAD_IRMS_A_MAX_A);
            const bool no_load_power = (fabsf(snap.active_power_w) <= METER_NO_LOAD_ACTIVE_POWER_W_MAX);
            if (no_load_current && no_load_power) {
                snap.irms_a = 0.0f;
                snap.irms_b = 0.0f;
                snap.active_power_w = 0.0f;
                snap.reactive_power_var = 0.0f;
                snap.apparent_power_va = 0.0f;
                snap.power_factor = 0.0f;
                s_have_pf_valid = false;
                s_pf_hold_samples = 0U;
            } else {
                const bool pf_near_zero = (fabsf(snap.power_factor) <= METER_PF_INVALID_ABS_MAX);
                float pf_fallback = 0.0f;
                const bool has_fallback = task_measurement_pf_fallback_from_vi(&snap, &pf_fallback);

                if (pf_near_zero && has_fallback) {
                    snap.power_factor = pf_fallback;
                    s_last_pf_valid = snap.power_factor;
                    s_have_pf_valid = true;
                    s_pf_hold_samples = 0U;
                } else if (!pf_near_zero) {
                    s_last_pf_valid = snap.power_factor;
                    s_have_pf_valid = true;
                    s_pf_hold_samples = 0U;
                } else if (s_have_pf_valid && (s_pf_hold_samples < METER_PF_HOLD_MAX_SAMPLES)) {
                    snap.power_factor = s_last_pf_valid;
                    ++s_pf_hold_samples;
                } else {
                    s_have_pf_valid = false;
                }
            }
        }

        if (!ac_present) {
            // Si no hay AC, se normalizan magnitudes electricas para evitar lecturas basura por ruido.
            task_measurement_zero_electrical_snapshot(&snap, false);
            s_have_pf_valid = false;
            s_pf_hold_samples = 0U;
        }

        // Energia software integrada desde potencia instantanea filtrada.
        const bool freq_valid_for_energy =
            (snap.frequency >= METER_AC_PRESENT_FREQ_MIN_HZ) &&
            (snap.frequency <= METER_AC_PRESENT_FREQ_MAX_HZ);
        const bool vrms_valid_for_energy =
            (snap.vrms >= METER_AC_PRESENT_VRMS_MIN_V) &&
            (snap.vrms <= METER_VRMS_SANITY_MAX_V);
        if (ac_present &&
            freq_valid_for_energy &&
            vrms_valid_for_energy &&
            (snap.active_power_w > METER_NO_LOAD_ACTIVE_POWER_W_MAX)) {
            const float delta_wh = snap.active_power_w * ((float)cycle_ms / 3600000.0f);
            s_energy_wh_soft += delta_wh;
            s_energy_wh_total_nvs += delta_wh;
        }
        if (s_energy_wh_soft < 0.0f) {
            s_energy_wh_soft = 0.0f;
        }
        if (s_energy_wh_total_nvs < 0.0f) {
            s_energy_wh_total_nvs = 0.0f;
        }

        task_measurement_energy_nvs_try_save(ac_changed && !ac_present);

        snap.energy_soft_wh = s_energy_wh_soft;
        snap.energy_total_nvs_wh = s_energy_wh_total_nvs;
        // Campo legacy: por compatibilidad se publica energia total persistente.
        snap.energy_wh = snap.energy_total_nvs_wh;
        snap.ac_present = ac_present ? 1U : 0U;

        if (meter_data_update(&snap) != ESP_OK) {
            LOG_WARN(TAG, "meter_data_update failed - cycle continues");
        }

#if FEATURE_METER_EVENTS_PUBLISH
        const bool publish_event = ac_changed || ((cycle_counter % log_divider) == 0U);
        if (publish_event) {
            MeterEvent_t ev = {
                .timestamp_us = snap.timestamp_us,
                .event_type = METER_EVENT_NEW_DATA
            };
            if (q_meter_events != NULL) {
                if (xQueueSend(q_meter_events, &ev, 0) != pdTRUE) {
                    LOG_WARN(TAG, "q_meter_events full - event discarded");
                }
            }
        }
#endif

        prev_ac_present = ac_present;
        if (ac_changed || ((cycle_counter % log_divider) == 0U)) {
            LOG_INFO(TAG,
                     "medicion: AC=%s Vrms=%.2fV IrmsA=%.3fA P=%.2fW PF=%.3f F=%.2fHz TEMP=%.2fC TEMP_OK=%u E=%.4fWh E_hw=%.6f E_soft=%.4f E_nvs=%.4f CAL=0x%02X RAW_VRMS_CUR=%lu RAW_OK=%u RAW_VRMS_LAST=%lu RAW_APERIOD=%lu APERIOD_OK=%u ADE_OK=%u RAW_EHI=%ld",
                     task_measurement_ac_status_text(s_ade_ready_confirmed, ac_present),
                     (double)snap.vrms,
                     (double)snap.irms_a,
                     (double)snap.active_power_w,
                     (double)snap.power_factor,
                     (double)snap.frequency,
                     (double)snap.temperature,
                     (unsigned int)snap.temperature_valid,
                     (double)snap.energy_wh,
                     (double)snap.energy_hw_wh,
                     (double)snap.energy_soft_wh,
                     (double)snap.energy_total_nvs_wh,
                     (unsigned int)snap.calibration_status,
                     (unsigned long)vrms_raw,
                     (unsigned int)(vrms_raw_valid_now ? 1U : 0U),
                     (unsigned long)s_last_vrms_raw,
                     (unsigned long)aperiod_raw,
                     (unsigned int)(aperiod_raw_valid ? 1U : 0U),
                     (unsigned int)(s_ade_ready_confirmed ? 1U : 0U),
                     (long)s_last_energy_hi_raw);
        }

        ++cycle_counter;

        const TickType_t now_ticks = xTaskGetTickCount();
        const int32_t jitter_ticks = (int32_t)now_ticks - (int32_t)xLastWakeTime;
        const int32_t tick_ms = (int32_t)portTICK_PERIOD_MS;
        const int32_t jitter_ms = jitter_ticks * tick_ms;
        LOG_DEBUG(TAG, "cycle done t=%lldus jitter=%ldms", (long long)snap.timestamp_us, (long)jitter_ms);
    }
}






















