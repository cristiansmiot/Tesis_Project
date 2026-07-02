#include "task_communication.h"

#include <stdbool.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include "ade9153a_init.h"
#include "data_serializer.h"
#include "ds3231.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "meter_config.h"
#include "meter_data.h"         // meter_data_get_snapshot()
#include "node_health.h"
#include "sd_storage.h"
#include "task_calibration.h"   // task_calibration_get_status(), CALIB_STATUS_*
#include "task_monitor.h"
#include "../modules/communication/mqtt_client.h"
#include "../modules/communication/mqtt_topics.h"
#include "rtos_app_config.h"
#include "sim7080g_init.h"
#include "logger.h"

static const char *TAG = "task_communication";
static TaskHandle_t s_task_handle;
static bool s_subscribed;
static volatile bool s_force_estado; // set by "status" cmd → publica /estado inmediato

/**
 * Sincroniza el reloj del sistema y el RTC DS3231 con la hora de red del
 * SIM7080G (AT+CCLK). Con el sistema en hora, las mediciones bufferizadas
 * en SD llevan epoch real y el nodo conserva la hora entre reinicios
 * gracias a la bateria del DS3231.
 */
static void task_communication_apply_network_time(void)
{
    if (sim7080g_sync_network_time() != ESP_OK) {
        LOG_WARN(TAG, "CCLK sin hora de red valida");
        return;
    }
    const time_t epoch = sim7080g_get_network_epoch();
    if (epoch < (time_t)METER_TIME_VALID_MIN_EPOCH) {
        return;
    }

    const struct timeval tv = { .tv_sec = epoch, .tv_usec = 0 };
    (void)settimeofday(&tv, NULL);

    if (ds3231_is_ready()) {
        struct tm utc = {0};
        (void)gmtime_r(&epoch, &utc);
        if (ds3231_set_time(&utc) != ESP_OK) {
            LOG_WARN(TAG, "DS3231 no acepto la hora de red");
        }
    }
    LOG_INFO(TAG, "hora sincronizada desde red celular: %s UTC",
             sim7080g_get_network_time());
}

/** Consumidor del backlog: re-publica una linea SenML guardada en SD. */
static esp_err_t task_communication_backlog_publish(const char *line, void *ctx)
{
    (void)ctx;
    return mqtt_client_publish(mqtt_topics_datos(), line,
                               METER_MQTT_PUBLISH_QOS, 0);
}

/**
 * Guarda la medicion actual en el backlog de la SD cuando MQTT no esta
 * disponible. Limitada a una muestra por METER_COMM_PUBLISH_PERIOD_MS para
 * que los reintentos rapidos (backoff de 2-12 s) no inflen el archivo con
 * lecturas casi identicas. Requiere hora de sistema valida: una medicion
 * sin timestamp real llegaria al backend con la hora equivocada.
 */
static void task_communication_backlog_store(void)
{
    static TickType_t s_last_store_tick;

    if (!sd_storage_is_mounted()) {
        return;
    }
    const time_t now = time(NULL);
    if (now < (time_t)METER_TIME_VALID_MIN_EPOCH) {
        return;
    }
    const TickType_t tick = xTaskGetTickCount();
    if ((s_last_store_tick != 0U) &&
        ((tick - s_last_store_tick) < pdMS_TO_TICKS(METER_COMM_PUBLISH_PERIOD_MS))) {
        return;
    }

    const MeterData_t snap = meter_data_get_snapshot();
    char line[768];
    if (data_serializer_build_senml_datos_at(&snap, (double)now,
                                             line, sizeof(line)) != ESP_OK) {
        return;
    }
    if (sd_storage_append_line(METER_SD_BACKLOG_FILE, line) == ESP_OK) {
        s_last_store_tick = tick;
        LOG_INFO(TAG, "medicion guardada en backlog SD (MQTT caido)");
    }
}

/**
 * @brief Procesa comando recibido via MQTT desde el dashboard.
 * Comandos: reset, calibrate, probe, status, relay_off, relay_on, sync_time.
 */
static void task_communication_process_cmd(const char *cmd)
{
    if (cmd == NULL) {
        return;
    }
    LOG_INFO(TAG, "processing remote cmd: %s", cmd);

    if (strcmp(cmd, "calibrate") == 0) {
        const uint8_t cal_st = task_calibration_get_status();
        if ((cal_st & CALIB_STATUS_RUNNING) != 0U) {
            LOG_WARN(TAG, "calibration already running");
        } else if (task_calibration_start() == ESP_OK) {
            LOG_INFO(TAG, "calibration started via remote cmd");
        } else {
            LOG_ERROR(TAG, "calibration start failed via remote cmd");
        }
    } else if (strcmp(cmd, "probe") == 0) {
        const esp_err_t err = ade9153a_recover(ADE9153A_RECOVERY_ACTION_PROBE);
        if (err == ESP_OK) {
            LOG_INFO(TAG, "ADE probe OK via remote cmd");
        } else {
            LOG_WARN(TAG, "ADE probe failed via remote cmd: %s", esp_err_to_name(err));
        }
    } else if (strcmp(cmd, "reset") == 0) {
        LOG_WARN(TAG, "remote reset requested - restarting ESP32");
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
    } else if (strcmp(cmd, "status") == 0) {
        s_force_estado = true;
        LOG_INFO(TAG, "status cmd: /estado will be published immediately");
    } else if (strcmp(cmd, "relay_off") == 0) {
        // TODO: abrir GPIO del rele de corte cuando el hardware este cableado
        LOG_WARN(TAG, "relay_off: GPIO relay not wired yet");
    } else if (strcmp(cmd, "relay_on") == 0) {
        // TODO: cerrar GPIO del rele de suministro cuando el hardware este cableado
        LOG_WARN(TAG, "relay_on: GPIO relay not wired yet");
    } else if (strcmp(cmd, "sync_time") == 0) {
        // El comando llego por MQTT, asi que la sesion celular esta viva
        // y AT+CCLK responde con la hora de red.
        task_communication_apply_network_time();
    } else {
        LOG_WARN(TAG, "unknown remote cmd: %s", cmd);
    }
}

static void task_communication(void *pvParameters)
{
    (void)pvParameters;

    if (!METER_COMM_ENABLE) {
        LOG_WARN(TAG, "communication disabled by config");
        vTaskDelete(NULL);
        return;
    }

    if (data_serializer_init() != ESP_OK) {
        LOG_ERROR(TAG, "data_serializer_init failed");
        vTaskDelete(NULL);
        return;
    }

    (void)node_health_init();

    bool mqtt_profile_ready = false;
    bool mqtt_session_ready = false;
    // 768 B: cabe /datos (~350 B), /estado (~540 B con IMEI) y /alerta (~300 B).
    // Antes era 512 y /estado llegaba a truncarse (len=535).
    char payload[768];
    // Snapshot compartido entre todos los topics del ciclo actual.
    MeterData_t snap = {0};
    // Timer para publicacion periodica del topic /estado (salud del nodo).
    TickType_t last_estado_tick = 0U;
    // Backoff progresivo para reintentos de conexion.
    uint8_t consecutive_failures = 0U;

    for (;;) {
        task_monitor_heartbeat(TASK_MON_COMMUNICATION);
        bool fast_retry = false;

        // ?????? Gestion de sesion MQTT ??????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????
        // Evita ciclos AT innecesarios cuando ya hay sesion activa.
        // Solo hace bring-up/recovery completo cuando no hay sesion local.
        if (!mqtt_session_ready) {
            if (sim7080g_init() != ESP_OK) {
                LOG_WARN(TAG, "modem init failed - retry");
                mqtt_profile_ready = false;
                fast_retry = true;
                goto cycle_delay;
            }

            // Con el modem arriba el IMEI ya se leyo: fijar la identidad
            // MQTT del nodo (topics medidor/<IMEI>/*) antes de conectar.
            (void)mqtt_topics_init();

            if (!sim7080g_is_pdp_ready()) {
                node_health_cellular_attempt_inc();
                if (sim7080g_recover_network() != ESP_OK) {
                    LOG_WARN(TAG, "network recovery failed");
                    mqtt_profile_ready = false;
                    fast_retry = true;
                    goto cycle_delay;
                }
                node_health_cellular_success_inc();
            }

            if (!mqtt_profile_ready) {
                if (mqtt_client_init() != ESP_OK) {
                    LOG_WARN(TAG, "mqtt_client_init failed");
                    mqtt_profile_ready = false;
                    fast_retry = true;
                    goto cycle_delay;
                }
                mqtt_profile_ready = true;
            }
            if (mqtt_client_connect() != ESP_OK) {
                LOG_WARN(TAG, "mqtt connect failed - recover");
                if (mqtt_client_recover() != ESP_OK) {
                    LOG_WARN(TAG, "mqtt recover failed");
                    mqtt_session_ready = false;
                    mqtt_profile_ready = false;
                    fast_retry = true;
                    goto cycle_delay;
                }
            }

            mqtt_session_ready = true;

            // ?????? /conexion: publicar "online" al establecer sesion ?????????????????????????????????
            // retain=1 / QoS=1: el broker retiene el ultimo estado conocido
            // para suscriptores que se conecten despues.
            // El broker publica "offline" (LWT) si el nodo cae sin SMDISC.
            {
                const esp_err_t con_err = mqtt_client_publish(
                    mqtt_topics_conexion(), "online",
                    METER_MQTT_LWT_QOS, METER_MQTT_LWT_RETAIN);
                if (con_err != ESP_OK) {
                    LOG_WARN(TAG, "/conexion online publish failed: %s",
                             esp_err_to_name(con_err));
                } else {
                    LOG_INFO(TAG, "/conexion online publicado");
                }
            }

            // Sincronizar fecha/hora de red tras establecer sesion
            // (sistema + RTC DS3231; habilita el timestamping del backlog).
            task_communication_apply_network_time();

            // ?????? /cmd: suscribirse al topico de comandos remotos ??????????????????????????????
            {
                const esp_err_t sub_err = mqtt_client_subscribe(mqtt_topics_cmd());
                if (sub_err != ESP_OK) {
                    LOG_WARN(TAG, "subscribe to %s failed: %s",
                             mqtt_topics_cmd(), esp_err_to_name(sub_err));
                    s_subscribed = false;
                } else {
                    s_subscribed = true;
                }
            }
        }

        // ?????? Tomar snapshot unico para todos los topics de este ciclo ????????????????????????
        snap = meter_data_get_snapshot();

        // ?????? /datos: mediciones electricas en SenML RFC 8428 ???????????????????????????????????????????????????
        if (data_serializer_build_senml_datos(&snap, payload, sizeof(payload)) != ESP_OK) {
            LOG_WARN(TAG, "SenML /datos serialization failed");
            fast_retry = true;
            goto cycle_delay;
        }

        {
            const esp_err_t pub_err = mqtt_client_publish(mqtt_topics_datos(),
                                                          payload,
                                                          METER_MQTT_PUBLISH_QOS,
                                                          METER_MQTT_PUBLISH_RETAIN);
            if (pub_err != ESP_OK) {
                LOG_WARN(TAG, "/datos publish failed err=%s - forcing recovery",
                         esp_err_to_name(pub_err));

                // Timeout transitorio: si la sesion sigue activa no forzar recovery duro.
                if ((pub_err == ESP_ERR_TIMEOUT) && mqtt_client_check_connected()) {
                    LOG_WARN(TAG, "publish timeout, MQTT still connected - skip hard recovery");
                    fast_retry = true;
                    goto cycle_delay;
                }

                mqtt_session_ready = false;
                if (mqtt_client_recover() != ESP_OK) {
                    LOG_WARN(TAG, "mqtt recover failed");
                    mqtt_profile_ready = false;
                } else {
                    mqtt_session_ready = true;
                }
                fast_retry = true;
                goto cycle_delay;
            }
        }

        node_health_msg_tx_inc();
        LOG_INFO(TAG, "/datos SenML publicado topic=%s", mqtt_topics_datos());

        // ── Drenar backlog SD acumulado durante caidas de MQTT ───────────
        // Lotes pequenos por ciclo para no retrasar la telemetria en vivo.
        if (sd_storage_is_mounted()) {
            size_t pendientes = 0U;
            const esp_err_t drain_err = sd_storage_drain_lines(
                METER_SD_BACKLOG_FILE, METER_SD_BACKLOG_DRAIN_PER_CYCLE,
                task_communication_backlog_publish, NULL, &pendientes);
            if ((drain_err == ESP_OK) && (pendientes > 0U)) {
                LOG_INFO(TAG, "backlog SD: %u mediciones aun pendientes",
                         (unsigned)pendientes);
            }
        }

        // ?????? /alerta: evento de calidad de potencia (event-driven) ?????????????????????????????????
        // Solo se publica si hay flags PQ activos en el snapshot actual.
        // QoS=1 para asegurar entrega; sin retain (el evento es puntual).
        if (snap.pq_status != 0U) {
            if (data_serializer_build_senml_alerta(&snap, snap.pq_status,
                                                   payload, sizeof(payload)) == ESP_OK) {
                const esp_err_t al_err = mqtt_client_publish(
                    mqtt_topics_alerta(), payload, 1, 0);
                if (al_err != ESP_OK) {
                    LOG_WARN(TAG, "/alerta publish failed: %s", esp_err_to_name(al_err));
                } else {
                    LOG_INFO(TAG, "/alerta publicado pq=0x%lx", (unsigned long)snap.pq_status);
                }
            }
        }

        // ?????? /estado: salud del nodo cada METER_COMM_ESTADO_PERIOD_MS ????????????????????????
        // retain=1 / QoS=1: el broker retiene el ultimo estado para que
        // el backend conozca la situacion del nodo sin esperar el ciclo.
        // s_force_estado=true cuando llega cmd "status" para publicacion inmediata.
        {
            const TickType_t now_tick = xTaskGetTickCount();
            const bool force = s_force_estado;
            if (force) { s_force_estado = false; }
            if (force || (now_tick - last_estado_tick) >= pdMS_TO_TICKS(METER_COMM_ESTADO_PERIOD_MS)) {
                const uint8_t cal_st = task_calibration_get_status();
                const bool cal_ok =
                    ((cal_st & CALIB_STATUS_VALID)     != 0U) &&
                    ((cal_st & CALIB_STATUS_LOADED)    != 0U) &&
                    ((cal_st & CALIB_STATUS_ERROR)     == 0U);

                const bool cellular_ok = sim7080g_is_network_ready() && sim7080g_is_pdp_ready();
                const bool mqtt_ok_now = mqtt_client_is_connected();

                if (data_serializer_build_senml_estado(&snap,
                                                       cellular_ok,
                                                       mqtt_ok_now,
                                                       cal_ok,
                                                       payload,
                                                       sizeof(payload)) == ESP_OK) {
                    const esp_err_t st_err = mqtt_client_publish(
                        mqtt_topics_estado(), payload,
                        METER_MQTT_LWT_QOS, METER_MQTT_LWT_RETAIN);
                    if (st_err != ESP_OK) {
                        LOG_WARN(TAG, "/estado publish failed: %s", esp_err_to_name(st_err));
                    } else {
                        last_estado_tick = now_tick;
                        LOG_INFO(TAG, "/estado SenML publicado");
                    }
                }
            }
        }

cycle_delay:
        if (fast_retry) {
            // Sin MQTT: preservar la medicion en SD para entrega diferida.
            task_communication_backlog_store();
            if (consecutive_failures < UINT8_MAX) {
                ++consecutive_failures;
            }
            uint32_t backoff_ms = METER_COMM_RECOVERY_RETRY_MS;
            uint8_t shift = (consecutive_failures > 0U) ? (uint8_t)(consecutive_failures - 1U) : 0U;
            if (shift > 3U) {
                shift = 3U;
            }
            backoff_ms <<= shift;
            if (backoff_ms > METER_COMM_RECOVERY_RETRY_MAX_MS) {
                backoff_ms = METER_COMM_RECOVERY_RETRY_MAX_MS;
            }
            vTaskDelay(pdMS_TO_TICKS(backoff_ms));
        } else {
            consecutive_failures = 0U;
            // Heartbeat entre publishes: verifica salud del modem cada
            // METER_COMM_HEARTBEAT_INTERVAL_MS en vez de dormir 60s ciego.
            const TickType_t period_ticks = pdMS_TO_TICKS(METER_COMM_PUBLISH_PERIOD_MS);
            const TickType_t hb_ticks = pdMS_TO_TICKS(METER_COMM_HEARTBEAT_INTERVAL_MS);
            const TickType_t start_tick = xTaskGetTickCount();
            while ((xTaskGetTickCount() - start_tick) < period_ticks) {
                const TickType_t remaining = period_ticks - (xTaskGetTickCount() - start_tick);
                const TickType_t sleep = (remaining < hb_ticks) ? remaining : hb_ticks;
                vTaskDelay(sleep);
                task_monitor_heartbeat(TASK_MON_COMMUNICATION);

                // ?????? Revisar comandos remotos pendientes ??????????????????????????????????????????
                if (s_subscribed) {
                    char cmd[64];
                    if (mqtt_client_read_command(cmd, sizeof(cmd)) == ESP_OK) {
                        task_communication_process_cmd(cmd);
                    }
                }

                if (mqtt_session_ready && !mqtt_client_check_connected()) {
                    LOG_WARN(TAG, "heartbeat: MQTT session lost - triggering recovery");
                    mqtt_session_ready = false;
                    mqtt_profile_ready = false;
                    s_subscribed = false;
                    break;
                }
            }
        }
    }
}

/**
 * @brief Crea y arranca la tarea de comunicacion.
 * @return ESP_OK en caso de exito.
 */
esp_err_t task_communication_start(void)
{
    if (s_task_handle != NULL) {
        return ESP_OK;
    }

    const BaseType_t rc = xTaskCreatePinnedToCore(
        task_communication,
        "task_communication",
        TASK_STACK_COMM,
        NULL,
        TASK_PRIO_COMMUNICATION,
        &s_task_handle,
        1);

    if (rc != pdPASS) {
        LOG_ERROR(TAG, "task_communication create failed");
        s_task_handle = NULL;
        return ESP_FAIL;
    }

    LOG_INFO(TAG, "task_communication started on core 1");
    return ESP_OK;
}



