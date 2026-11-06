#ifndef METER_CONFIG_H
#define METER_CONFIG_H

// Parametros electricos
#define METER_VNOM_V              120.0f
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

// Ajuste de ganancia del canal de tension (registro AVGAIN del ADE9153A).
// Ganancia efectiva = 1 + AVGAIN / 2^27. Se escribe en cada inicializacion y
// recuperacion del ADE; una calibracion mSure persistida en NVS la sustituye.
// Origen del valor (unidad SM-348518435344, EV-ADE9153ASHIELDZ): corrida
// NOCHE_12h_metro_20261103_run01, 18 ciclos contra Keysight 34450A en el rango
// ACV 100 V (U = 0,34 %): error medio del DUT +0,265 % en 100/108/115 V.
// AVGAIN = round((1/1,00265 - 1) * 2^27) = -354737. 0 = sin ajuste.
#ifndef METER_ADE_AVGAIN_BENCH
#define METER_ADE_AVGAIN_BENCH    (-354737L)
#endif

// Umbrales PQ
#define METER_SAG_THRESHOLD_PCT   0.10f
#define METER_SWELL_THRESHOLD_PCT 0.10f
#define METER_OVERTEMP_C          85.0f

// mSure
#define METER_MSURE_CERT_PPM      3000
// Los registros CC/CERT no se actualizan antes de 8 s. Se deja 1 s de margen.
#define METER_MSURE_INIT_WAIT_MS  9000U
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
#ifndef METER_STORAGE_ENABLE
#define METER_STORAGE_ENABLE                    1
#endif
#ifndef METER_SD_BASE_DIR
#define METER_SD_BASE_DIR                      "smartmeter"
#endif
#define METER_SD_HISTORY_FILE                  METER_SD_BASE_DIR "/history_v1.csv"
#define METER_SD_TELEMETRY_AUDIT_FILE          METER_SD_BASE_DIR "/telemetry_audit_v1.jsonl"
#define METER_SD_TELEMETRY_QUEUE_FILE          METER_SD_BASE_DIR "/telemetry_v1.jsonl"
#define METER_SD_ALERT_QUEUE_FILE              METER_SD_BASE_DIR "/alerts_v1.jsonl"
#define METER_SD_EVENT_LOG_FILE                METER_SD_BASE_DIR "/events_v1.jsonl"
/* Adaptadores de nombre para perfiles de hardware existentes. */
#define METER_SD_BACKLOG_FILE                  METER_SD_TELEMETRY_QUEUE_FILE
#define METER_SD_ALERT_BACKLOG_FILE            METER_SD_ALERT_QUEUE_FILE
#define METER_SD_BACKLOG_DRAIN_PER_CYCLE       8U
// Sobrescribible desde platformio.ini: el perfil de metrologia registra mas
// rapido para reunir 30 pares por punto sin alargar cada escalon.
#ifndef METER_SD_HISTORY_PERIOD_MS
#define METER_SD_HISTORY_PERIOD_MS             60000U
#endif
#define METER_SD_USAGE_REFRESH_MS              10000U
#define METER_SD_ALERT_RESERVE_MB               64U
#define METER_SD_HISTORY_PURGE_BATCH            256U
#define METER_SD_TELEMETRY_PURGE_BATCH          32U
// Epoch minimo para considerar valida la hora del sistema (2020-01-01).
// Sin hora valida no se bufferiza: una medicion sin timestamp real
// llegaria al backend con hora de recepcion equivocada.
#define METER_TIME_VALID_MIN_EPOCH             1577836800LL
#define METER_RTC_POLL_MS                       60000U
#define METER_RTC_RECOVERY_SLOW_MS              60000U
#define METER_TASK_HB_TIMEOUT_TIME_MS            15000U
#define METER_TIME_CELLULAR_RETRY_MS             60000U
#define METER_TIME_CELLULAR_RESYNC_MS         21600000U

// Sprint 3 - Comunicaciones SIM7080G + MQTT
// 0 = sin modem ni MQTT (perfil de metrologia en banco): la tarea de
// comunicaciones se autoelimina y la microSD queda como unico registro.
#ifndef METER_COMM_ENABLE
#define METER_COMM_ENABLE                      1
#endif
#define METER_COMM_PUBLISH_PERIOD_MS           60000U
#define METER_COMM_RECOVERY_RETRY_MS           2000U
#define METER_COMM_RECOVERY_RETRY_MAX_MS       30000U
// Corte prolongado de datos (registro y PDP activos, pero el broker no es
// alcanzable): tras este tiempo los reintentos pasan a un ritmo lento para
// no generar señalizacion inutil; el reintento liviano detecta el regreso.
#define METER_COMM_OUTAGE_SLOW_AFTER_MS        600000U
#define METER_COMM_OUTAGE_SLOW_RETRY_MS        60000U
// Intervalo de heartbeat AT entre publishes para detectar perdida del modem.
// Si el modem no responde al health-check, dispara recovery inmediato
// en vez de esperar al siguiente publish.
#define METER_COMM_HEARTBEAT_INTERVAL_MS       15000U
// Periodo de publicacion del topic /estado (salud del nodo).
// Independiente del ciclo de /datos. Usar multiplo del publish period.
// rc4: el perfil de produccion lo sube a 30 min para ahorrar datos (~1,1 KB
// por /estado); sobrescribible desde platformio.ini.
#ifndef METER_COMM_ESTADO_PERIOD_MS
#define METER_COMM_ESTADO_PERIOD_MS            300000U   // 5 minutos
#endif
// Modo de depuracion Sprint 3:
// 1 = prioriza SIM7080G (omite ADE/metrologia y arranca solo task_communication)
// 0 = arranque normal completo (ADE + metrologia + comunicacion)
#ifndef METER_SIM7080G_FOCUS_MODE
#define METER_SIM7080G_FOCUS_MODE              0
#endif

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
// Tras un SMPUB/SMCONN colgado el modem puede quedar varios minutos sin
// contestar AT mientras su stack TCP reintenta (medido: 2-4.5 min). Si el
// modem respondio por UART dentro de esta ventana se considera ocupado:
// no se barren baudios ni se reporta SIM_NO_AT.
#define METER_SIM7080G_BUSY_GRACE_MS           300000U
// El HAT Waveshare recibe PWR mediante transistor NPN: GPIO4 debe permanecer
// LOW y un pulso HIGH equivale a presionar el boton. La tarea prueba UART
// varias veces antes de tocar PWRKEY y limita los pulsos durante una averia.
#ifndef METER_SIM7080G_AUTO_PWRKEY_RECOVERY
#define METER_SIM7080G_AUTO_PWRKEY_RECOVERY     1
#endif
#define METER_SIM7080G_PWRKEY_NO_AT_PROBES     3U
#define METER_SIM7080G_PWRKEY_PULSE_MS         3000U
#define METER_SIM7080G_PWRKEY_BOOT_WAIT_MS     30000U
#define METER_SIM7080G_PWRKEY_MAX_ATTEMPTS     2U
#define METER_SIM7080G_PWRKEY_SLOW_RETRY_MS    60000U
// Silencio UART minimo antes de un pulso PWRKEY: un pulso sobre un modem
// ocupado (encendido) lo apaga.
#define METER_SIM7080G_PWRKEY_MIN_SILENCE_MS   600000U
// rc4: tras un reinicio solo del ESP32 (software, watchdog, panico, EN) el
// modem pudo quedar encendido y ocupado. Si aun no ha contestado AT en este
// arranque, se exige este silencio antes de pulsar. Tras un encendido en
// frio (ESP_RST_POWERON) el HAT tambien perdio energia: queda apagado y se
// enciende tras METER_SIM7080G_PWRKEY_NO_AT_PROBES sondeos sin respuesta.
#define METER_SIM7080G_PWRKEY_WARM_BOOT_SILENCE_MS 300000U
// Perfil sin comunicaciones (METER_COMM_ENABLE=0): 1 = apagar el modem al
// arrancar (AT+CPOWD=1, PWRKEY si no obedece). Un modem encendido sigue
// registrado y su cliente MQTT envia keepalives: consume datos del plan.
#ifndef METER_SIM7080G_POWER_OFF_WHEN_DISABLED
#define METER_SIM7080G_POWER_OFF_WHEN_DISABLED 0
#endif

// ── rc4: preferencia de operador ────────────────────────────────────────
// La SIM (IMSI 214-07) solo puede registrarse en Movistar Colombia (732-123).
// Colombia Movil (732-103/111) la rechaza con 5004 INVALID AVP VALUE (Kite,
// E4-01) y el modem queda sin servicio mientras reintenta. Valor no vacio:
// AT+COPS=4,2,"<plmn>" (manual con respaldo automatico del propio modem).
// Vacio: AT+COPS=0. COPS se guarda en el modem (AUTO_SAVE), por eso el
// firmware lo verifica en cada inicializacion completa.
// Banco rc4 (2026-11-06, banco_run02): con COPS=4,2,"732123" el modem guarda
// modo manual (+COPS: 1) y, tras CFUN o reinicio, queda sin buscar red
// (CEREG 0, CPSI NO SERVICE) incluso despues de un N3; en automatico se
// registra solo en 732-123. Por eso rc4 usa "" (COPS=0), que ademas repara
// un modem que quedo guardado en manual.
#ifndef METER_SIM7080G_PREFERRED_PLMN
#define METER_SIM7080G_PREFERRED_PLMN          ""
#endif
#define METER_SIM7080G_COPS_TIMEOUT_MS         120000U

// ── rc4: escalera de recuperacion celular ───────────────────────────────
// Se mide el tiempo desde la ultima publicacion MQTT confirmada (no desde la
// ultima "sesion": en E4-01 hubo sesiones que fallaban al primer SMPUB).
// Solo actua si el modem contesta AT; el modem mudo sigue la politica
// PWRKEY por NO_AT. Escalones (cada uno reinicia los relojes de los menores):
//   N1 radio off/on (AT+CFUN=4/1)       desde N1_AFTER, cada N1_MIN_GAP
//   N2 reinicio software (AT+CFUN=1,1)   desde N2_AFTER, cada N2_MIN_GAP
//   N3 apagado + PWRKEY (reinicio fisico) desde N3_AFTER, cada N3_GAP (se
//      duplica hasta N3_MAX_GAP) y como maximo N3_MAX_PER_DAY por 24 h
// Con registro y PDP activos se hace antes una sonda de datos (ping y DNS):
// si responde, la falla es del broker/servidor y no se toca el modem salvo
// un N2 tras PROBE_OK_N2_AFTER (pila MQTT del modem trabada).
#ifndef METER_CELL_RECOVERY_ENABLE
#define METER_CELL_RECOVERY_ENABLE             1
#endif
#ifndef METER_CELL_RECOVERY_N1_AFTER_MS
#define METER_CELL_RECOVERY_N1_AFTER_MS        600000U     // 10 min
#endif
// Sin registro y con CEREG 0 (no busca) o 3 (denegado): N1 adelantado.
#ifndef METER_CELL_RECOVERY_N1_EARLY_MS
#define METER_CELL_RECOVERY_N1_EARLY_MS        180000U     // 3 min
#endif
#ifndef METER_CELL_RECOVERY_N1_MIN_GAP_MS
#define METER_CELL_RECOVERY_N1_MIN_GAP_MS      1200000U    // 20 min
#endif
#ifndef METER_CELL_RECOVERY_N2_AFTER_MS
#define METER_CELL_RECOVERY_N2_AFTER_MS        1800000U    // 30 min
#endif
#ifndef METER_CELL_RECOVERY_N2_MIN_GAP_MS
#define METER_CELL_RECOVERY_N2_MIN_GAP_MS      3600000U    // 60 min
#endif
#ifndef METER_CELL_RECOVERY_N3_AFTER_MS
#define METER_CELL_RECOVERY_N3_AFTER_MS        5400000U    // 90 min
#endif
#ifndef METER_CELL_RECOVERY_N3_GAP_MS
#define METER_CELL_RECOVERY_N3_GAP_MS          7200000U    // 2 h
#endif
#ifndef METER_CELL_RECOVERY_N3_MAX_GAP_MS
#define METER_CELL_RECOVERY_N3_MAX_GAP_MS      28800000U   // 8 h
#endif
#define METER_CELL_RECOVERY_N3_MAX_PER_DAY     6U
#ifndef METER_CELL_RECOVERY_PROBE_MIN_GAP_MS
#define METER_CELL_RECOVERY_PROBE_MIN_GAP_MS   300000U     // 5 min
#endif
// Con la sonda correcta y MQTT fallando, N2 llega a los 45 min: mas que un
// corte de broker de E5 (10 y 30 min, sin reinicios) y menos que dejar 2 h
// una pila MQTT del modem trabada ("operation not allowed" con ping OK, visto
// en la primera conexion del banco rc4, 2026-11-05).
#ifndef METER_CELL_RECOVERY_PROBE_OK_N2_AFTER_MS
#define METER_CELL_RECOVERY_PROBE_OK_N2_AFTER_MS 2700000U  // 45 min
#endif
#define METER_CELL_RECOVERY_PING_HOST          "8.8.8.8"
#define METER_CELL_RECOVERY_DNS_HOST           "google.com"
// 1 solo en el entorno de banco: autoprueba de la politica al arrancar y
// comandos remotos de inyeccion de fallas (cell_fault:<s>, mqtt_fault:<s>).
#ifndef METER_CELL_RECOVERY_TEST_HOOKS
#define METER_CELL_RECOVERY_TEST_HOOKS         0
#endif

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
// /datos   : QoS 1, retain=0, cada 60 s; conservar cola hasta verificar sesion
// /estado  : QoS 1, retain=1, cada 5 min (snapshot, no presencia)
// /alerta  : QoS 1, retain=0, event-driven
// Presencia: el backend actualiza last_seen con trafico vivo y vence el nodo
// por inactividad; este firmware de SIM7080G no admite LWT de forma fiable.
#define METER_MQTT_ID_PREFIX                   "SM"
#define METER_MQTT_DEVICE_ID_FALLBACK          "ESP32-001"

#define METER_MQTT_STATUS_QOS                  1
#define METER_MQTT_STATUS_RETAIN               1

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
// En cortes de datos el modem puede completar un SMCONN abandonado minutos
// despues (medido: 4.5 min); esa sesion tardia se adopta en vez de cerrarla
// si aparece dentro de METER_MQTT_SMCONN_LATE_ADOPT_MS.
#define METER_MQTT_SMCONN_TIMEOUT_MS           45000U
#define METER_MQTT_SMCONN_POSTWAIT_MS          15000U
#define METER_MQTT_SMCONN_RETRIES              0U
#define METER_MQTT_SMCONN_LATE_ADOPT_MS        600000U
// Ciclar el PDP no restablece un corte de datos de la red y cada ciclo
// genera re-registros: como maximo uno por ventana mientras no haya sesion.
#define METER_MQTT_PDP_CYCLE_COOLDOWN_MS       1800000U
#ifndef METER_MQTT_PUBLISH_QOS
#define METER_MQTT_PUBLISH_QOS                 1
#endif
#define METER_MQTT_PUBLISH_RETAIN              0
#define METER_MQTT_PUBLISH_TIMEOUT_MS          15000U
// El firmware R1951.01 del SIM7080G usado en el prototipo rechaza SMPUB por
// encima de 1024 bytes aunque revisiones posteriores del manual anuncien un
// limite mayor. Incluye los dos bytes CRLF enviados despues del JSON.
#define METER_MQTT_SMPUB_SAFE_MAX_BYTES        1024U
// 1 = en cada recovery limpia sesion y reprovisiona perfil MQTT.
// 0 = solo reprovisiona cuando el perfil local se invalida.
#define METER_MQTT_AGGRESSIVE_RECOVERY         0

// Version de firmware incluida en payload /estado (campo SenML "fw").
// Actualizar con cada release del firmware.
#ifndef METER_FW_VERSION
#define METER_FW_VERSION                       "1.1.0-obj4"
#endif

// Sprint 4 - UI
#define METER_UI_ENABLE                        1
#define METER_UI_POLL_PERIOD_MS                100U
#define METER_UI_INPUT_POLL_PERIOD_MS          20U
#define METER_UI_PERIODIC_REFRESH_MS           500U
#define METER_UI_ACTION_STATUS_HOLD_MS         2500U
#define METER_UI_SPLASH_DURATION_MS            6000U
#define METER_UI_IDLE_HOME_TIMEOUT_MS          20000U
#define METER_UI_OLED_RECOVERY_FAST_MS         2000U

// Normalizacion de telemetria - SenML RFC 8428 activo
// Unidades SenML usadas: V, A, W, VAR, VA, /, Hz, Wh, Cel, count
// Referencia: https://www.iana.org/assignments/senml/senml.xhtml
#define METER_TELEMETRY_FORMAT_FLAT_JSON       0U
#define METER_TELEMETRY_FORMAT_SENML_JSON      1U
#define METER_TELEMETRY_FORMAT                 METER_TELEMETRY_FORMAT_SENML_JSON

#endif // METER_CONFIG_H

