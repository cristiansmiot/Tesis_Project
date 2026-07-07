#ifndef METER_CONFIG_H
#define METER_CONFIG_H

// Parametros electricos
#define METER_VNOM_V              220.0f
#define METER_IMAX_A              10.0f
#define METER_LINE_FREQ_HZ        60
// Deteccion de presencia de linea usando AVRMS crudo (sin conversion a volts).
// Ajustable por hardware durante bring-up.
// Segun logs de bring-up:
// - AC presente estable: RAW_VRMS ~ 6.9e6 .. 9.3e6
// - AC ausente estable:  RAW_VRMS ~ 8.1e2 .. 9.5e2
// Se usan dos umbrales para evitar oscilaciones (histeresis).
#define METER_AC_PRESENT_VRMS_RAW_ON_MIN   100000U
#define METER_AC_PRESENT_VRMS_RAW_LOST_MAX 10000U
// Umbrales de apoyo usando VRMS convertido.
#define METER_AC_PRESENT_VRMS_MIN_V         20.0f
#define METER_AC_LOST_VRMS_MAX_V            10.0f
// Banda valida de frecuencia para deteccion.
#define METER_AC_PRESENT_FREQ_MIN_HZ        45.0f
#define METER_AC_PRESENT_FREQ_MAX_HZ        65.0f
// Tiempo base de adquisicion/deteccion.
#define METER_MEASUREMENT_PERIOD_MS         250U
// Debounce de presencia AC (en muestras).
#define METER_AC_DEBOUNCE_ON_SAMPLES        2U
#define METER_AC_DEBOUNCE_LOST_SAMPLES      2U
// Recuperacion automatica ante metrologia congelada (VRMS/F en cero con RAW_VRMS alto previo).
#define METER_AC_STALL_RECOVERY_SAMPLES     4U
#define METER_AC_STALL_RECOVERY_COOLDOWN_MS 10000U
// Recuperacion automatica ante bus ADE no valido desde arranque o runtime.
// Se requieren mas muestras invalidas antes de disparar recovery para
// tolerar glitches transitorios de SPI sin resetear el ADE.
// INVALID_RECOVERY_SAMPLES reducido a 4: con periodo 250ms, dispara recovery
// en ~1s tras perder el ADE (vs ~2s con 8 muestras).
// ESCALATE_AFTER=0: salta SOFT_INIT y va directo a SPI_REINIT, porque en
// las pruebas reales SOFT_INIT nunca recupera el bus (siempre lee 0x0000)
// y solo SPI_REINIT reconstruye el peripheral SPI correctamente.
#define METER_ADE_INVALID_RECOVERY_SAMPLES  4U
#define METER_ADE_RECOVERY_COOLDOWN_MS      4000U
#define METER_ADE_RECOVERY_ESCALATE_AFTER   0U
// Backoff: tras N fallos consecutivos, reducir frecuencia de recovery
// para no martillar el bus y dejar que Vcc se estabilice.
#define METER_ADE_RECOVERY_MAX_FAST_RETRIES 4U
#define METER_ADE_RECOVERY_SLOW_COOLDOWN_MS 60000U
// Filtro de no-carga para limpiar ruido en vacio.
#define METER_NO_LOAD_IRMS_A_MAX_A          0.010f
#define METER_NO_LOAD_ACTIVE_POWER_W_MAX    1.00f
// Guardas de robustez para PF: detectar APF espurio bajo carga real.
#define METER_PF_INVALID_ABS_MAX            0.020f
#define METER_PF_FALLBACK_MIN_ACTIVE_POWER_W 2.00f
#define METER_PF_FALLBACK_MIN_APPARENT_POWER_VA 4.00f
#define METER_PF_HOLD_MAX_SAMPLES           8U
// Limite superior de sanidad para VRMS convertido.
#define METER_VRMS_SANITY_MAX_V             300.0f
#define METER_ACTIVE_POWER_SANITY_MAX_W     (METER_VRMS_SANITY_MAX_V * METER_IMAX_A * 2.0f)
#define METER_REACTIVE_POWER_SANITY_MAX_VAR (METER_VRMS_SANITY_MAX_V * METER_IMAX_A * 2.0f)
#define METER_APPARENT_POWER_SANITY_MAX_VA  (METER_VRMS_SANITY_MAX_V * METER_IMAX_A * 2.0f)
#define METER_TEMP_SANITY_MIN_C             (-40.0f)
#define METER_TEMP_SANITY_MAX_C             125.0f
// Correccion de polaridad (CT/orientacion). Usar +1.0f o -1.0f.
#define METER_POWER_SIGN_CORRECTION         (-1.0f)
// Energia hibrida y persistencia NVS.
#define METER_ENERGY_NVS_NAMESPACE          "meter"
#define METER_ENERGY_NVS_KEY_TOTAL_WH       "e_total_wh"
#define METER_ENERGY_NVS_SAVE_PERIOD_MS     5000U
#define METER_ENERGY_NVS_SAVE_MIN_DELTA_WH  0.001f

// Hardware eval board
#define METER_AI_PGAGAIN          16
#define METER_BI_PGAGAIN          1
#define METER_RSHUNT_OHM          0.001f
#define METER_RSMALL_OHM          1000.0f
#define METER_RBIG_OHM            1000000.0f

// Umbrales PQ
#define METER_SAG_THRESHOLD_PCT   0.10f
#define METER_SWELL_THRESHOLD_PCT 0.10f
#define METER_OVERTEMP_C          85.0f

// mSure
#define METER_MSURE_CERT_PPM      3000
#define METER_MSURE_INIT_WAIT_MS  8000
#define METER_MSURE_READY_TIMEOUT_MS 1500U
#define METER_MSURE_USE_TURBO     0
#define METER_MSURE_REAPPLY_ON_BOOT 1
#define METER_MSURE_AUTORUN_ON_BOOT 0
#define METER_MSURE_BOOT_WAIT_AC_TIMEOUT_MS 15000U
#define METER_MSURE_BOOT_STABLE_SAMPLES 8U
#define METER_MSURE_CC_MIN_RATIO 0.25f
#define METER_MSURE_CC_MAX_RATIO 4.00f
#define METER_CALIB_NVS_NAMESPACE "calib"
#define METER_CALIB_NVS_KEY_RECORD "msure_v1"

// Monitor PQ
#define METER_PQ_MONITOR_PERIOD_MS 250U

// Supervision de tareas (watchdog por software, ver task_monitor.h)
// Revision del supervisor cada 5 s. Umbrales por tarea dimensionados al
// peor caso de cada ciclo, no al periodo nominal:
// - medicion/PQ: ciclo de 250 ms, pero un recovery de ADE puede tomar segundos.
// - comunicacion: un bring-up completo del SIM7080G (autoprobe de baud +
//   registro de red con METER_SIM7080G_NETWORK_TIMEOUT_MS=120 s) puede
//   superar los 3 minutos; el umbral debe quedar por encima.
// - UI: poll de teclado continuo, umbral corto.
#define METER_TASK_MONITOR_PERIOD_MS           5000U
#define METER_TASK_HB_TIMEOUT_MEASUREMENT_MS   15000U
#define METER_TASK_HB_TIMEOUT_PQ_MS            15000U
#define METER_TASK_HB_TIMEOUT_COMM_MS          300000U
#define METER_TASK_HB_TIMEOUT_UI_MS            15000U
// Plazo de gracia antes de escalar a esp_restart() cuando una tarea
// sigue sin latido. 10 minutos: suficiente para descartar recoveries
// largos del modem y aun asi recuperar el nodo en campo sin visita.
#define METER_TASK_STALL_REBOOT_MS             600000U
// Marca de agua de stacks: reporte cada N revisiones del supervisor
// (60 x 5 s = 5 min) y umbral de advertencia en palabras libres.
#define METER_TASK_STACK_LOG_EVERY             60U
#define METER_TASK_STACK_WARN_WORDS            256U

// Buffer offline en MicroSD (store-and-forward)
// Cuando MQTT no esta disponible las mediciones se anexan a un archivo
// JSONL en la SD; al recuperar sesion se drenan en lotes pequenos para
// no retrasar la telemetria en vivo.
#define METER_SD_BACKLOG_FILE                  "backlog.jsonl"
#define METER_SD_BACKLOG_DRAIN_PER_CYCLE       8U
// Epoch minimo para considerar valida la hora del sistema (2020-01-01).
// Sin hora valida no se bufferiza: una medicion sin timestamp real
// llegaria al backend con hora de recepcion equivocada.
#define METER_TIME_VALID_MIN_EPOCH             1577836800LL

// Sprint 3 - Comunicaciones SIM7080G + MQTT
#define METER_COMM_ENABLE                      1
#define METER_COMM_PUBLISH_PERIOD_MS           60000U
#define METER_COMM_RECOVERY_RETRY_MS           2000U
#define METER_COMM_RECOVERY_RETRY_MAX_MS       12000U
// Intervalo de heartbeat AT entre publishes para detectar perdida del modem.
// Si el modem no responde al health-check, dispara recovery inmediato
// en vez de esperar al siguiente publish.
#define METER_COMM_HEARTBEAT_INTERVAL_MS       15000U
// Periodo de publicacion del topic /estado (salud del nodo).
// Independiente del ciclo de /datos. Usar multiplo del publish period.
#define METER_COMM_ESTADO_PERIOD_MS            300000U   // 5 minutos
// Modo de depuracion Sprint 3:
// 1 = prioriza SIM7080G (omite ADE/metrologia y arranca solo task_communication)
// 0 = arranque normal completo (ADE + metrologia + comunicacion)
#define METER_SIM7080G_FOCUS_MODE              0

#define METER_SIM7080G_APN                     "internet.movistar.com.co"
// SIM7080G usa normalmente:
// - CGDCONT con contexto PDP (1..16)
// - CNACT/CNCFG con perfil de app network 0
#define METER_SIM7080G_PDP_CONTEXT_ID          1U
#define METER_SIM7080G_PDP_CID                 0U
#define METER_SIM7080G_CNMP_MODE               38U
// CMNB=3 permite CAT-M + NB-IoT automatico. Movistar Colombia no tiene
// cobertura NB-IoT masiva; con CMNB=2 (NB-IoT only) el modem reportaba
// NO SERVICE indefinidamente. En modo auto cae a CAT-M y registra.
#define METER_SIM7080G_CMNB_MODE               3U
#define METER_SIM7080G_AUTO_APN_QUERY          1
#define METER_SIM7080G_BAUD_AUTOPROBE          1
#define METER_SIM7080G_AT_TIMEOUT_MS           4000U
#define METER_SIM7080G_MAX_RETRIES             3U
#define METER_SIM7080G_NETWORK_TIMEOUT_MS      120000U

#define METER_MQTT_HOST                        "shinkansen.proxy.rlwy.net"
#define METER_MQTT_PORT                        58954U
#define METER_MQTT_USERNAME                    "medidor_iot"
#define METER_MQTT_PASSWORD                    "Colombia2026$"

// ── Identidad del nodo (plug and play) ──────────────────────────────────
// device_id = METER_MQTT_ID_PREFIX + MAC eFuse del ESP32-S3 (inmutable de
// fabrica): sobrevive cambios de SIM y de modem, y esta disponible desde
// el boot. Los topics medidor/<id>/* y el CLIENTID se construyen en
// runtime (ver mqtt_topics.h); un mismo binario sirve para toda la flota.
// El IMEI viaja como metadato en /estado, no como identidad.
// QoS/retain por topic:
// /datos   : QoS 0, retain=0, cada 60 s
// /estado  : QoS 1, retain=1, cada 5 min
// /alerta  : QoS 1, retain=0, event-driven
// /conexion: QoS 1, retain=1 (online / LWT offline)
#define METER_MQTT_ID_PREFIX                   "SM"
#define METER_MQTT_DEVICE_ID_FALLBACK          "ESP32-001"

// LWT (Last Will and Testament): el broker publica "offline" en /conexion
// si el nodo cae sin desconectarse correctamente (sin AT+SMDISC explicito).
#define METER_MQTT_LWT_QOS                     1
#define METER_MQTT_LWT_RETAIN                  1

#define METER_MQTT_KEEPALIVE_S                 120U

// ── TLS nodo→broker (AT+CSSLCFG / AT+SMSSL) ─────────────────────────────
// 0 = MQTT en claro (solo usuario/contraseña). 1 = TLS 1.2 validando el
// broker contra la CA propia embebida en config/mqtt_ca_cert.h.
// Activar SOLO despues de desplegar los certificados en el broker
// (mosquitto-config/TLS_DEPLOY.md) y apuntar METER_MQTT_PORT al listener
// TLS; si no, SMCONN fallara el handshake en cada reconexion.
#define METER_MQTT_USE_TLS                     0
// Nombre del archivo CA dentro del FS del modem (directorio /customer/).
#define METER_MQTT_TLS_CA_NAME                 "ca.crt"
// Contexto SSL del modem usado para MQTT (0..5).
#define METER_MQTT_TLS_CTX                     0U
// 3 = TLS 1.2 en AT+CSSLCFG="SSLVERSION". El SIM7080G no soporta TLS 1.3.
#define METER_MQTT_TLS_VERSION                 3U
// Escritura del PEM al FS del modem (archivo ~2 KB por UART a 57600).
#define METER_MQTT_TLS_FSWRITE_TIMEOUT_MS      10000U
// SMCONN timing tunning:
// El modem tipicamente completa la conexion TCP+MQTT en ~30-38 s (DNS celular
// + TCP handshake + MQTT CONNECT/CONNACK). Con RETRIES=1 el HAL bloqueaba
// 2x20s = 40s antes de ceder el control. Con RETRIES=0 y timeout de 30s
// el primer intento cubre la mayoria de los casos. Si supera 30s, la ventana
// postwait de 15s atrapa la conexion retardada (~35-38s desde SMCONN).
// Tiempo total esperado: ~34-38s vs ~42s anterior.
#define METER_MQTT_SMCONN_TIMEOUT_MS           30000U
#define METER_MQTT_SMCONN_POSTWAIT_MS          10000U
#define METER_MQTT_SMCONN_RETRIES              0U
#define METER_MQTT_PUBLISH_QOS                 0
#define METER_MQTT_PUBLISH_RETAIN              0
#define METER_MQTT_PUBLISH_TIMEOUT_MS          15000U
// 1 = en cada recovery limpia sesion y reprovisiona perfil MQTT.
// 0 = solo reprovisiona cuando el perfil local se invalida.
#define METER_MQTT_AGGRESSIVE_RECOVERY         0

// Version de firmware incluida en payload /estado (campo SenML "fw").
// Actualizar con cada release del firmware.
#define METER_FW_VERSION                       "1.0.0"

// Sprint 4 - UI
#define METER_UI_ENABLE                        1
#define METER_UI_POLL_PERIOD_MS                100U
#define METER_UI_INPUT_POLL_PERIOD_MS          20U
#define METER_UI_PERIODIC_REFRESH_MS           500U
#define METER_UI_ACTION_STATUS_HOLD_MS         2500U
#define METER_UI_SPLASH_DURATION_MS            6000U
#define METER_UI_IDLE_HOME_TIMEOUT_MS          20000U

// Normalizacion de telemetria - SenML RFC 8428 activo
// Unidades SenML usadas: V, A, W, VAR, VA, /, Hz, Wh, Cel, count
// Referencia: https://www.iana.org/assignments/senml/senml.xhtml
#define METER_TELEMETRY_FORMAT_FLAT_JSON       0U
#define METER_TELEMETRY_FORMAT_SENML_JSON      1U
#define METER_TELEMETRY_FORMAT                 METER_TELEMETRY_FORMAT_SENML_JSON

#endif // METER_CONFIG_H


