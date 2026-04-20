#include "ade9153a_init.h"

#include <math.h>
#include <string.h>
#include "ade9153a_hal.h"
#include "ade9153a_regs.h"
#include "ade9153a_spi.h"
#include "meter_config.h"
#include "logger.h"

static const char *TAG = "ade9153a_init";
static bool s_initialized;
static ade9153a_diag_t s_last_diag;

// Valores base de referencia: ADE9153AAPI.h (Analog Devices).
static const uint16_t k_ai_pgagain_default = 0x000AU;  // Senal en IAN, ganancia 16x.
static const uint16_t k_bi_pgagain_default = 0x0000U;  // Ganancia BI por defecto (x1).
static const uint32_t k_config0_default = 0x00000000U;
static const uint16_t k_config1_default = 0x0300U;
static const uint16_t k_config2_default = 0x0C00U;
static const uint16_t k_config3_default = 0x0000U;
static const uint16_t k_accmode_default = 0x0000U;  // Base 50 Hz.
static const uint32_t k_vlevel_default = 0x002C11E8U;
static const uint16_t k_zx_cfg_default = 0x0000U;
static const uint32_t k_mask_default = 0x00000100U;
static const uint32_t k_act_nl_lvl_default = 0x000033C8U;
static const uint32_t k_react_nl_lvl_default = 0x000033C8U;
static const uint32_t k_app_nl_lvl_default = 0x000033C8U;
static const uint16_t k_compmode_default = 0x0005U;
static const uint32_t k_vdiv_rsmall_default = 0x000003E8U;
static const uint16_t k_ep_cfg_default = 0x0009U;
static const uint16_t k_egy_time_default = 0x0F9FU;
static const uint16_t k_temp_cfg_default = 0x000CU;

static const char *ade9153a_action_name(ade9153a_recovery_action_t action)
{
    switch (action) {
        case ADE9153A_RECOVERY_ACTION_PROBE:
            return "PROBE";
        case ADE9153A_RECOVERY_ACTION_SOFT_INIT:
            return "SOFT_INIT";
        case ADE9153A_RECOVERY_ACTION_SPI_REINIT:
            return "SPI_REINIT";
        default:
            return "UNKNOWN";
    }
}

/**
 * @brief Valida PRODUCT_ID contemplando posibles variantes de orden de bytes.
 * @param product Valor leido de VERSION_PRODUCT.
 * @return true si coincide con el ID esperado en alguna variante conocida.
 */
static bool ade9153a_product_id_matches(uint32_t product)
{
    if (product == ADE9153A_PRODUCT_ID) {
        return true;
    }

    // Variante con bytes invertidos.
    if (__builtin_bswap32(product) == ADE9153A_PRODUCT_ID) {
        return true;
    }

    // Variante con palabras de 16 bits intercambiadas.
    const uint32_t swapped16 = ((product & 0x0000FFFFU) << 16U) | ((product & 0xFFFF0000U) >> 16U);
    return (swapped16 == ADE9153A_PRODUCT_ID);
}

/**
 * @brief Detecta patrones tipicos de bus SPI sin respuesta real del ADE9153A.
 * @param run Valor de REG_RUN.
 * @param version Valor de REG_VERSION.
 * @param product Valor de REG_VERSION_PRODUCT.
 * @param chip_hi Valor de REG_CHIP_ID_HI.
 * @param chip_lo Valor de REG_CHIP_ID_LO.
 * @return true si los patrones sugieren fallo de enlace fisico SPI.
 */
static bool ade9153a_bus_looks_disconnected(
    uint16_t run,
    uint16_t version,
    uint32_t product,
    uint32_t chip_hi,
    uint32_t chip_lo
)
{
    const bool all_zero = (run == 0x0000U) &&
                          (version == 0x0000U) &&
                          (product == 0x00000000U) &&
                          (chip_hi == 0x00000000U) &&
                          (chip_lo == 0x00000000U);

    const bool all_one = (run == 0xFFFFU) &&
                         (version == 0xFFFFU) &&
                         (product == 0xFFFFFFFFU) &&
                         (chip_hi == 0xFFFFFFFFU) &&
                         (chip_lo == 0xFFFFFFFFU);

    return all_zero || all_one;
}

static void ade9153a_diag_store(const ade9153a_diag_t *diag)
{
    if (diag == NULL) {
        return;
    }
    s_last_diag = *diag;
    s_last_diag.initialized = s_initialized;
}

static esp_err_t ade9153a_probe_diag(ade9153a_diag_t *out, bool verbose_log)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ade9153a_diag_t diag = {0};
    diag.initialized = s_initialized;
    diag.recovery_count = s_last_diag.recovery_count;
    diag.last_action = s_last_diag.last_action;
    diag.probe_err = ESP_FAIL;

    if (ade9153a_read_reg16(ADE9153A_REG_RUN, &diag.run) != ESP_OK) {
        diag.probe_err = ESP_ERR_INVALID_STATE;
        ade9153a_diag_store(&diag);
        *out = diag;
        if (verbose_log) {
            LOG_WARN(TAG, "fallo lectura REG_RUN");
        }
        return diag.probe_err;
    }
    if (ade9153a_read_reg16(ADE9153A_REG_VERSION, &diag.version) != ESP_OK) {
        diag.probe_err = ESP_ERR_INVALID_STATE;
        ade9153a_diag_store(&diag);
        *out = diag;
        if (verbose_log) {
            LOG_WARN(TAG, "fallo lectura REG_VERSION");
        }
        return diag.probe_err;
    }
    if (ade9153a_read_reg32(ADE9153A_REG_VERSION_PRODUCT, &diag.product) != ESP_OK) {
        diag.probe_err = ESP_ERR_INVALID_STATE;
        ade9153a_diag_store(&diag);
        *out = diag;
        if (verbose_log) {
            LOG_WARN(TAG, "fallo lectura REG_VERSION_PRODUCT");
        }
        return diag.probe_err;
    }
    if (ade9153a_read_reg32(ADE9153A_REG_CHIP_ID_HI, &diag.chip_hi) != ESP_OK) {
        diag.probe_err = ESP_ERR_INVALID_STATE;
        ade9153a_diag_store(&diag);
        *out = diag;
        if (verbose_log) {
            LOG_WARN(TAG, "fallo lectura REG_CHIP_ID_HI");
        }
        return diag.probe_err;
    }
    if (ade9153a_read_reg32(ADE9153A_REG_CHIP_ID_LO, &diag.chip_lo) != ESP_OK) {
        diag.probe_err = ESP_ERR_INVALID_STATE;
        ade9153a_diag_store(&diag);
        *out = diag;
        if (verbose_log) {
            LOG_WARN(TAG, "fallo lectura REG_CHIP_ID_LO");
        }
        return diag.probe_err;
    }

    diag.bus_disconnected = ade9153a_bus_looks_disconnected(diag.run,
                                                            diag.version,
                                                            diag.product,
                                                            diag.chip_hi,
                                                            diag.chip_lo);

    if (verbose_log) {
        LOG_INFO(TAG,
                 "diagnostico ADE9153A: RUN=0x%04x VERSION=0x%04x PRODUCT=0x%08lx CHIP_HI=0x%08lx CHIP_LO=0x%08lx",
                 (unsigned int)diag.run,
                 (unsigned int)diag.version,
                 (unsigned long)diag.product,
                 (unsigned long)diag.chip_hi,
                 (unsigned long)diag.chip_lo);
    }

    if (diag.bus_disconnected) {
        diag.ready = false;
        diag.probe_err = ESP_ERR_INVALID_RESPONSE;
        ade9153a_diag_store(&diag);
        *out = diag;
        if (verbose_log) {
            LOG_WARN(TAG, "patron SPI invalido (todo 0x00 o todo 0xFF) - revisar alimentacion, CS, MISO, SCLK y RESET");
        }
        return diag.probe_err;
    }

    if ((diag.run & (uint16_t)ADE9153A_RUN_ON) == 0U) {
        diag.ready = false;
        diag.probe_err = ESP_ERR_INVALID_RESPONSE;
        ade9153a_diag_store(&diag);
        *out = diag;
        if (verbose_log) {
            LOG_WARN(TAG, "RUN bit no activo (REG_RUN=0x%04x)", (unsigned int)diag.run);
        }
        return diag.probe_err;
    }

    const bool has_chip_id = ((diag.chip_hi != 0x00000000U) && (diag.chip_hi != 0xFFFFFFFFU)) ||
                             ((diag.chip_lo != 0x00000000U) && (diag.chip_lo != 0xFFFFFFFFU));
    if (!ade9153a_product_id_matches(diag.product) && !has_chip_id) {
        diag.ready = false;
        diag.probe_err = ESP_ERR_INVALID_RESPONSE;
        ade9153a_diag_store(&diag);
        *out = diag;
        if (verbose_log) {
            LOG_WARN(TAG,
                     "identificacion invalida: PRODUCT=0x%08lx CHIP_HI=0x%08lx CHIP_LO=0x%08lx",
                     (unsigned long)diag.product,
                     (unsigned long)diag.chip_hi,
                     (unsigned long)diag.chip_lo);
        }
        return diag.probe_err;
    }

    if ((diag.product != ADE9153A_PRODUCT_ID) && ade9153a_product_id_matches(diag.product) && verbose_log) {
        LOG_WARN(TAG, "PRODUCT_ID coincide solo con variante endian: 0x%08lx", (unsigned long)diag.product);
    }

    diag.ready = true;
    diag.probe_err = ESP_OK;
    ade9153a_diag_store(&diag);
    *out = diag;
    return ESP_OK;
}

/**
 * @brief Aplica secuencia base de configuracion para Sprint 1.
 * @param void Sin parametros.
 * @return ESP_OK en caso de exito.
 */
static esp_err_t ade9153a_apply_defaults(void)
{
    esp_err_t err = ade9153a_set_pgagain((uint8_t)k_ai_pgagain_default, (uint8_t)k_bi_pgagain_default);
    if (err != ESP_OK) {
        return err;
    }

    err = ade9153a_write_reg32(ADE9153A_REG_CONFIG0, k_config0_default);
    if (err != ESP_OK) {
        return err;
    }
    err = ade9153a_write_reg16(ADE9153A_REG_CONFIG1, k_config1_default);
    if (err != ESP_OK) {
        return err;
    }
    err = ade9153a_write_reg16(ADE9153A_REG_CONFIG2, k_config2_default);
    if (err != ESP_OK) {
        return err;
    }
    err = ade9153a_write_reg16(ADE9153A_REG_CONFIG3, k_config3_default);
    if (err != ESP_OK) {
        return err;
    }
    err = ade9153a_write_reg16(ADE9153A_REG_ACCMODE, k_accmode_default);
    if (err != ESP_OK) {
        return err;
    }
    err = ade9153a_write_reg32(ADE9153A_REG_VLEVEL, k_vlevel_default);
    if (err != ESP_OK) {
        return err;
    }
    err = ade9153a_write_reg16(ADE9153A_REG_ZX_CFG, k_zx_cfg_default);
    if (err != ESP_OK) {
        return err;
    }
    err = ade9153a_write_reg32(ADE9153A_REG_MASK, k_mask_default);
    if (err != ESP_OK) {
        return err;
    }
    err = ade9153a_write_reg32(ADE9153A_REG_ACT_NL_LVL, k_act_nl_lvl_default);
    if (err != ESP_OK) {
        return err;
    }
    err = ade9153a_write_reg32(ADE9153A_REG_REACT_NL_LVL, k_react_nl_lvl_default);
    if (err != ESP_OK) {
        return err;
    }
    err = ade9153a_write_reg32(ADE9153A_REG_APP_NL_LVL, k_app_nl_lvl_default);
    if (err != ESP_OK) {
        return err;
    }
    err = ade9153a_write_reg16(ADE9153A_REG_COMPMODE, k_compmode_default);
    if (err != ESP_OK) {
        return err;
    }
    err = ade9153a_write_reg32(ADE9153A_REG_VDIV_RSMALL, k_vdiv_rsmall_default);
    if (err != ESP_OK) {
        return err;
    }
    err = ade9153a_write_reg16(ADE9153A_REG_EP_CFG, k_ep_cfg_default);
    if (err != ESP_OK) {
        return err;
    }
    err = ade9153a_write_reg16(ADE9153A_REG_EGY_TIME, k_egy_time_default);
    if (err != ESP_OK) {
        return err;
    }
    return ade9153a_write_reg16(ADE9153A_REG_TEMP_CFG, k_temp_cfg_default);
}

/**
 * @brief Aplica configuracion base y arranca DSP (sin reset previo).
 * @return ESP_OK en caso de exito.
 */
static esp_err_t ade9153a_configure_and_start(void)
{
    esp_err_t err = ade9153a_apply_defaults();
    if (err != ESP_OK) {
        s_initialized = false;
        return err;
    }

    err = ade9153a_set_line_freq((uint8_t)METER_LINE_FREQ_HZ);
    if (err != ESP_OK) {
        s_initialized = false;
        return err;
    }

    err = ade9153a_start_dsp();
    if (err != ESP_OK) {
        s_initialized = false;
        return err;
    }

    s_initialized = true;
    s_last_diag.initialized = true;
    return ESP_OK;
}

/**
 * @brief Inicializa ADE9153A (reset + config + start DSP).
 * @param void Sin parametros.
 * @return ESP_OK en caso de exito.
 */
esp_err_t ade9153a_init(void)
{
    const ADE9153A_HAL_t *hal = ade9153a_hal_get();
    if (hal == NULL) {
        s_initialized = false;
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = ade9153a_reset();
    if (err != ESP_OK) {
        s_initialized = false;
        return err;
    }

    return ade9153a_configure_and_start();
}

/**
 * @brief Realiza reset fisico del ADE9153A.
 * @param void Sin parametros.
 * @return ESP_OK si pudo ejecutar la secuencia.
 */
esp_err_t ade9153a_reset(void)
{
    const ADE9153A_HAL_t *hal = ade9153a_hal_get();
    if (hal == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    // Espera pre-reset: si el ADE acaba de recibir alimentacion tras un
    // corte, Vcc necesita tiempo para estabilizarse. Sin esta espera,
    // el pulso de reset puede ocurrir antes de que el chip este activo
    // y no surte efecto (todo lee 0xFF despues).
    hal->platform_delay_ms(300U);

    // Secuencia alineada con referencia ADI/experiencia EV board: pulso largo de reset
    // y espera amplia de estabilizacion antes de la primera transaccion SPI.
    hal->reset_set(true);
    hal->platform_delay_ms(100U);
    hal->reset_set(false);
    hal->platform_delay_ms(1000U);
    return ESP_OK;
}

/**
 * @brief Arranca DSP de metrologia.
 * @param void Sin parametros.
 * @return ESP_OK en caso de exito.
 */
esp_err_t ade9153a_start_dsp(void)
{
    const ADE9153A_HAL_t *hal = ade9153a_hal_get();
    if (hal == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t err = ade9153a_write_reg16(ADE9153A_REG_RUN, (uint16_t)ADE9153A_RUN_ON);
    if (err != ESP_OK) {
        return err;
    }

    // Referencia ADE9153AAPI.cpp: esperar tras RUN=1 antes de leer VERSION_PRODUCT.
    // 200ms para dar margen al DSP para llenar filtros iniciales.
    hal->platform_delay_ms(200U);
    return ESP_OK;
}

/**
 * @brief Configura los registros de PGA de corriente.
 * @param gain_a Valor para AI_PGAGAIN.
 * @param gain_b Valor para BI_PGAGAIN.
 * @return ESP_OK en caso de exito.
 */
esp_err_t ade9153a_set_pgagain(uint8_t gain_a, uint8_t gain_b)
{
    // TODO: verificar codificacion completa de campos PGA contra datasheet ADE9153A.
    esp_err_t err = ade9153a_write_reg16(ADE9153A_REG_AI_PGAGAIN, (uint16_t)gain_a);
    if (err != ESP_OK) {
        return err;
    }
    return ade9153a_write_reg16(ADE9153A_REG_BI_PGAGAIN, (uint16_t)gain_b);
}

/**
 * @brief Configura frecuencia de linea en ACCMODE.
 * @param freq_hz Frecuencia deseada (50 o 60).
 * @return ESP_OK en caso de exito.
 */
esp_err_t ade9153a_set_line_freq(uint8_t freq_hz)
{
    uint16_t accmode = 0U;
    esp_err_t err = ade9153a_read_reg16(ADE9153A_REG_ACCMODE, &accmode);
    if (err != ESP_OK) {
        return err;
    }

    if (freq_hz == 60U) {
        accmode = (uint16_t)(accmode | (uint16_t)ADE9153A_BIT_SELFREQ);
    } else if (freq_hz == 50U) {
        accmode = (uint16_t)(accmode & (uint16_t)(~ADE9153A_BIT_SELFREQ));
    } else {
        return ESP_ERR_INVALID_ARG;
    }
    return ade9153a_write_reg16(ADE9153A_REG_ACCMODE, accmode);
}

/**
 * @brief Configura VLEVEL para potencia reactiva.
 * @param vnom Voltaje nominal.
 * @param vheadroom Factor de headroom.
 * @return ESP_OK en caso de exito.
 */
esp_err_t ade9153a_set_vlevel(float vnom, float vheadroom)
{
    if ((vnom <= 0.0f) || (vheadroom <= 0.0f)) {
        return ESP_ERR_INVALID_ARG;
    }

    // TODO: verificar formula generica de VLEVEL contra AN-1571.
    // Sprint 1 usa valor oficial de ADE9153AAPI para la EV board.
    const bool looks_like_eval = (fabsf(vnom - 220.0f) < 1.0f) && (fabsf(vheadroom - 1.47461f) < 0.01f);
    if (!looks_like_eval) {
        LOG_WARN(TAG, "Formula VLEVEL pendiente de verificacion; se usa valor EV por defecto");
    }
    return ade9153a_write_reg32(ADE9153A_REG_VLEVEL, k_vlevel_default);
}

/**
 * @brief Verifica firma de producto y estado RUN.
 * @param void Sin parametros.
 * @return true si ADE9153A responde correctamente.
 */
bool ade9153a_is_ready(void)
{
    ade9153a_diag_t diag = {0};
    return (ade9153a_probe_diag(&diag, true) == ESP_OK) && diag.ready;
}

esp_err_t ade9153a_get_diag(ade9153a_diag_t *out)
{
    return ade9153a_probe_diag(out, false);
}

esp_err_t ade9153a_recover(ade9153a_recovery_action_t action)
{
    LOG_WARN(TAG, "ADE9153A recovery action=%s", ade9153a_action_name(action));
    s_last_diag.last_action = action;

    if (action == ADE9153A_RECOVERY_ACTION_PROBE) {
        ade9153a_diag_t diag = {0};
        return ade9153a_probe_diag(&diag, true);
    }

    if (action == ADE9153A_RECOVERY_ACTION_SPI_REINIT) {
        const esp_err_t spi_err = ade9153a_spi_recover_bus();
        if (spi_err != ESP_OK) {
            return spi_err;
        }
    }

    // Fase 1: reset hardware con timing generoso (incluye pre-reset delay).
    const esp_err_t reset_err = ade9153a_reset();
    if (reset_err != ESP_OK) {
        return reset_err;
    }

    // Fase 2: verificar que el bus SPI responde antes de intentar configurar.
    // Tras un reset el ADE puede necesitar tiempo extra para estabilizar Vcc
    // y el oscilador interno. Se reintenta la lectura de VERSION hasta 3 veces
    // con esperas progresivas para cubrir arranques lentos.
    uint16_t ver_check = 0U;
    esp_err_t ver_err = ESP_FAIL;
    bool ver_ok = false;
    for (uint8_t ver_try = 0U; ver_try < 3U; ++ver_try) {
        if (ver_try > 0U) {
            const ADE9153A_HAL_t *hal_retry = ade9153a_hal_get();
            if (hal_retry != NULL) {
                hal_retry->platform_delay_ms(500U);
            }
        }
        ver_check = 0U;
        ver_err = ade9153a_read_reg16(ADE9153A_REG_VERSION, &ver_check);
        if ((ver_err == ESP_OK) && (ver_check != 0xFFFFU) && (ver_check != 0x0000U)) {
            ver_ok = true;
            break;
        }
        LOG_WARN(TAG,
                 "ADE VERSION read attempt %u/3: 0x%04x err=%s",
                 (unsigned)(ver_try + 1U),
                 (unsigned)ver_check,
                 esp_err_to_name(ver_err));
    }
    if (!ver_ok) {
        LOG_WARN(TAG,
                 "ADE no responde tras reset (VERSION=0x%04x err=%s) - sin alimentacion?",
                 (unsigned)ver_check,
                 esp_err_to_name(ver_err));
        ade9153a_diag_t fail_diag = {0};
        fail_diag.bus_disconnected = true;
        fail_diag.probe_err = ESP_ERR_INVALID_RESPONSE;
        fail_diag.last_action = action;
        fail_diag.recovery_count = s_last_diag.recovery_count;
        ade9153a_diag_store(&fail_diag);
        return ESP_ERR_INVALID_RESPONSE;
    }

    LOG_INFO(TAG, "ADE responde tras reset (VERSION=0x%04x) - configurando", (unsigned)ver_check);

    // Fase 3: bus vivo - aplicar configuracion completa y arrancar DSP.
    const esp_err_t cfg_err = ade9153a_configure_and_start();
    if (cfg_err != ESP_OK) {
        return cfg_err;
    }

    // Fase 4: diagnostico completo post-configuracion.
    ade9153a_diag_t diag = {0};
    const esp_err_t probe_err = ade9153a_probe_diag(&diag, true);
    if (probe_err == ESP_OK) {
        ++diag.recovery_count;
        diag.last_action = action;
        ade9153a_diag_store(&diag);
    }
    return probe_err;
}
