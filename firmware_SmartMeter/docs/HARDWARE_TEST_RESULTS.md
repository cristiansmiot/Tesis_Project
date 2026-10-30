# Registro de pruebas de hardware e integracion

## Compilacion de servicios SD/OLED (2026-10-18)

- Antes de modificar SD/OLED compilaron `test_system_contracts`,
  `test_oled_rtc_service` y `test_all_no_ade`.
- La matriz final compilo sin errores: `test_system_contracts`,
  `test_oled_rtc_service`, `test_sd_service`, `test_oled_rtc_sd_service` y
  `test_all_no_ade` (5/5).
- `test_sd_service`: 62 668 B RAM (19.1%) y 476 613 B flash (45.5%), despues
  de ampliar las pilas estaticas del perfil de prueba.
- `test_oled_rtc_sd_service`: 52 444 B RAM (16.0%) y 462 585 B flash (44.1%).
- `git diff --check` no encontro errores de whitespace; solo avisos de
  normalizacion CRLF/LF en cambios preexistentes.
- Las validaciones de banco de `test_sd_service` y
  `test_oled_rtc_sd_service` terminaron correctamente durante 30 minutos cada
  una. La OLED, la recuperacion despues de reinstalar la SD en frio y la
  inspeccion final de archivos en el PC tambien quedaron confirmadas.

Placa bajo prueba: PCB Smart Meter con ESP32-S3.

## Preparacion de prueba digital ADE9153A (2026-10-22)

- Se agrego el perfil aislado `test_ade_smoke`; compila correctamente con
  ESP-IDF 5.5 y PlatformIO.
- Uso de memoria: 72 564 B de RAM (22.1 %) y 480 149 B de flash (45.8 %).
- El perfil fuerza SD_CS alto, inicializa SPI2 a 1 MHz, aplica reset y
  configuracion base, lee RUN/VERSION/PRODUCT/CHIP_ID 30 veces y comprueba la
  temperatura interna del ADE9153A.
- Segun UG-1253 Rev. A, el EV-ADE9153ASHIELDZ usa alimentacion de 5 V y, con
  un MCU de 3.3 V, requiere abrir el jumper SPI de 5 V y cerrar el de 3 V.
- El perfil se cargo por COM6 y se ejecuto con LINE, NEUTRAL y carga
  desconectados; el usuario confirmo previamente jumper SPI en 3.3 V.
- Resultado fisico: **PASS**. Se observaron 30/30 lecturas coherentes con
  `RUN=0x0001`, `VERSION=0x0025`, `PRODUCT=0x0009153A`, `ready=1`,
  `bus_off=0` y `ESP_OK`. La temperatura interna fue 24.78 C y aprobaron
  `ADE SPI STABILITY`, `ADE TEMPERATURE` y `ADE_SMOKE`.
- El monitor continuo mostrando el ADE listo cada cinco segundos, sin perdida
  del bus durante la ventana observada. La prueba demuestra comunicacion SPI,
  identidad, arranque del DSP y sensor interno; no demuestra mSure, exactitud
  ni respuesta de las entradas analogicas.
- Siguiente puerta: integracion digital ADE + microSD + RTC + SIM sin AC.
  Despues, bajo montaje electrico protegido y supervisado, tension sin carga,
  carga resistiva baja y comparacion contra el instrumento de referencia.

## Diagnostico de contencion ADE-microSD (2026-10-22)

- Con ADE, RTC, SIM y la microSD completa conectados al mismo SPI2, el ADE
  devolvio `RUN=0x0000`, `VERSION=0x0000`, `PRODUCT=0x00000000` y `ADE ID FAIL`.
- Al retirar completamente la microSD, el ADE recupero `RUN=0x0001`,
  `VERSION=0x0025`, `ready=1`, `bus_off=0` y `ESP_OK` de forma sostenida.
- La prueba diferencial se repitio con la microSD alimentada y con MOSI, SCLK y
  CS conectados, dejando solamente `MISO/DO` aislado de GPIO13. El ADE volvio a
  aprobar y mantuvo lecturas consecutivas coherentes.
- Conclusion: existe contencion electrica en MISO del modulo microSD probado;
  no es una falla del ADE9153A ni de su enlace SPI.
- Para conservar la PCB ensamblada se adopta una correccion localizada: buffer
  triestado en la rama `MISO_SD`, con entrada desde DO de la tarjeta, salida al
  MISO compartido y habilitacion activa-baja desde `SD_CS`. Se mantienen SPI2,
  MOSI11/MISO13/SCLK12, CS_ADE10 y CS_SD7. Falta instalar el aislamiento y
  ejecutar la prueba integrada con los cuatro modulos.

## Estado

| Fecha | Perfil | Resultado | Evidencia | Pendiente |
| --- | --- | --- | --- | --- |
| 2026-10-18 | `test_sd_service` | PASS 30 min | El banco aprobo recuperacion `.tmp`, montaje/capacidad, CSV con hora valida e invalida, JSONL con `peek`/`ack`, productores concurrentes, saturacion intencional y umbrales 80/90% inyectados. Una primera ejecucion descubrio desbordamiento en la pila de la tarea auxiliar `sd_test_prod`; se ampliaron las pilas estaticas y se repitio desde cero. La ejecucion corregida mantuvo `mounted=1`, termino con 60 escrituras correctas, 0 fallidas, cola interna vacia, minimo de pila de `main` de 760 palabras y `HWTEST RESULT SD_SERVICE 30MIN PASS`, sin reinicios. | Revisar en PC el directorio `/codex_sd_test`, CSV/JSONL y ausencia de `.tmp`/`.bak` huerfanos cuando se apague la PCB de forma segura. |
| 2026-10-18 | `test_oled_rtc_sd_service` | PASS 30 min | Con el portabateria RTC vacio, `TIME` permanecio `FAILED` por fecha RTC no confiable y `CCLK` invalido sin impedir almacenamiento, OLED ni comunicaciones. La SD guardo y dreno telemetria/alertas por lotes. Se observaron dos perdidas reales de MQTT/SIM: una recuperacion MQTT de unos 12 s y otra recuperacion completa de SIM/LTE/MQTT de unos 85 s; durante esta ultima la telemetria se acumulo en SD y se dreno al reconectar. No hubo reinicios. Al minuto 30 el supervisor reporto 1900 palabras libres en comunicacion, 552 en UI y 516 en tiempo. El usuario confirmo OLED estable y sin parpadeo con `SIN FECHA`, `SD:OK`, `RTC/TIME:FAIL`, `CEL:OK`, `MQT:OK` y `ADE:--`. | Ensayar recuperacion tras reinstalar la SD con la PCB apagada y revisar los archivos en PC. |
| 2026-10-18 | `test_all_no_ade` sin microSD | PASS con correccion | Arranque y observacion de 5 minutos sin tarjeta: SD fallo de forma aislada, SIM/LTE/MQTT se recuperaron y `/datos` continuo publicandose; el supervisor reporto 1660 palabras libres en comunicacion y 548 en UI/tiempo, sin reinicios. La prueba descubrio eventos repetidos de saturacion y el intento redundante de inicializar SPI en cada remontaje. Se agrego un latch de una ocurrencia por episodio y se conserva el estado del bus SPI; la repeticion de tres ciclos ya no mostro `SPI bus already initialized` ni inundacion de `EVENT_QUEUE_OVERFLOW`. | Apagar, reinstalar la misma tarjeta y confirmar montaje, drenado y archivos validos. |
| 2026-10-18 | `test_all_no_ade` recuperacion microSD | PASS | Tras reinstalar la misma tarjeta con la PCB apagada, el servicio monto y leyo los backlogs existentes. Dreno alertas por lotes desde 5 pendientes y, despues de una caida transitoria del SIM/MQTT, guardo nuevas mediciones en la SD. Al recuperarse AT a 57600, LTE, PDP y MQTT, reanudo la publicacion y dreno tanto `alerts_v1.jsonl` como `telemetry_v1.jsonl`. El supervisor mantuvo 1900 palabras libres en comunicacion y 548 en UI/tiempo, sin reinicios. La indisponibilidad intermitente del SIM quedo aislada del almacenamiento. | Apagar la PCB, retirar la tarjeta en frio y validar en el PC CSV/JSONL y ausencia de temporales huerfanos. |
| 2026-10-18 | Inspeccion de microSD en PC | PASS | Windows reconocio la tarjeta como FAT32 con 7 736 393 728 bytes totales y 7 735 803 904 libres. Se encontraron 12 archivos y ningun `.tmp` o `.bak`. Los 335 registros JSONL de prueba y produccion se parsearon correctamente, terminaron en salto de linea, conservaron sus campos obligatorios y no repitieron la clave `boot + seq + type`. El historico de produccion contiene 78 filas, todas con las 19 columnas del encabezado versionado. Los registros con hora invalida usan `ts_utc=0` y `local` vacio. Quedaron 9 alertas y 1 telemetria pendientes, todas completas y recuperables en el siguiente arranque. | Reinstalar la tarjeta solamente con la PCB apagada; el servicio continuara el drenado de los diez registros pendientes. |
| 2026-10-18 | `test_all_no_ade` | PASS 10 min (revalidacion) | Perfil final cargado despues de integrar SD/OLED. OLED termino el splash y activo la pantalla principal; MQTT publico `/conexion`, `/datos` y `/estado`; la SD mantuvo el backlog drenado y `TIME` fallo de forma aislada. Dos ciclos del supervisor conservaron 2032 palabras libres en comunicacion, 552 en UI y 548 en tiempo, sin reinicios. Este perfil usa `METER_SIM7080G_FOCUS_MODE=1`, por lo que ADE, medicion, PQ y calibracion permanecen omitidos. | Ejecutar posteriormente las pruebas continuas de 2 h y 24 h; no interpretar este perfil como validacion de metrologia. |
| 2026-10-15 | `test_oled` | PASS | SSD1306 respondio en `0x3C`; ocho filas visibles; `RESULT PASS`; contador confirmado hasta 589 s; sin parpadeos, ruido ni reinicios. | Prueba conjunta. |
| 2026-10-15 | `test_rtc` | PASS | DS3231 respondio en `0x68`; contador avanzo 1 s/s; temperatura entre 22.75 C y 23.50 C; despues de retirar la alimentacion principal avanzo de `00:02:37` a `00:11:01` sin reiniciarse. | Ajustar o sincronizar fecha y hora. Actualmente reporta `2000-01-01`. |
| 2026-10-15 | `test_sd` | PASS | Tarjeta `SA08G` detectada con 7386 MB; FAT montado; 16/16 registros escritos y releidos; `7377/7378 MB` libres; presencia estable durante monitoreo. Se corrigieron el mapa a MOSI11/MISO13/SCLK12/CS7 y el soporte FATFS para nombres largos. | Prueba conjunta y verificacion posterior del archivo `hwtest_sd.txt` en un computador. |
| 2026-10-15 | `test_sim_at` | PASS | SIM7080G identificado como `R1951.01`; UART estable en GPIO16/GPIO17; SIM en estado `READY`; `CSQ: 31,99`; IMEI valido leido; tres ciclos AT consecutivos sin fallas ni reinicios. | Validar por separado el encendido/apagado mediante `PWRKEY` en GPIO4. |
| 2026-10-15 | `test_sim_network` | PASS con observacion | Registro LTE Cat-M1 por `CEREG` (`0,5`, roaming), operador Movistar, APN `internet.movistar.com.co`, PDP CID 0 activo y direccion IP asignada. RSSI `CSQ=31`. Hubo una desactivacion transitoria del PDP a los 45 s; el firmware lo recupero automaticamente y permanecio estable otros 136 s con comprobaciones cada 15 s. | Repetir una prueba prolongada con trafico MQTT/HTTP para determinar si la caida PDP fue un evento aislado o cierre por inactividad de la red. |
| 2026-10-16 | `test_sim_pwrkey` | FAIL de la ruta PWRKEY | GPIO4 se configuro open-drain y genero un pulso bajo de 1.5 s. Con el modem encendido, el pulso no lo apago. Luego `AT+CPOWD=1` si produjo `NORMAL POWER DOWN` y se confirmo ausencia de UART. Desde ese estado apagado, otro pulso bajo de GPIO4 no recupero UART durante 30 s. El modem si encendio con su boton PWRKEY y, despues, `test_sim_at` dio PASS: identificacion `R1951.01`, SIM `READY`, `CSQ: 28,99` e IMEI valido. Esto confirma que el modem, su boton y UART funcionan; la falla queda localizada en la ruta electrica GPIO4-PWR o en la polaridad/interfaz del adaptador. El ESP32 permanecio estable y GPIO4 quedo liberado en alta impedancia. | Con la alimentacion desconectada, comprobar continuidad GPIO4-PWR. Luego medir respecto de GND el voltaje de reposo del pin PWR y su nivel al pulsar el boton fisico. No probar salida activa alta de 3.3 V hasta identificar la interfaz. |
| 2026-10-16 | `test_sim_pwrkey` corregido | PASS | El esquema Waveshare confirmo que el pin externo `PWR` controla un transistor NPN y es activo en alto. Se corrigio GPIO4 a salida push-pull, LOW en reposo y pulso HIGH de 3 s. El primer pulso apago el modem y UART dejo de responder; el segundo lo encendio y se recuperaron identificacion `R1951.01`, SIM `READY` e IMEI valido. | Integrar esta polaridad en el arranque normal. El `CSQ: 99,99` inmediato al reencendido es esperado antes del registro de red; la prueba de red independiente ya fue PASS. |
| 2026-10-16 | `test_oled_rtc` | PASS | OLED SSD1306 `0x3C` y RTC DS3231 `0x68` funcionaron simultaneamente sobre SDA GPIO8/SCL GPIO9. Se corrigio la reutilizacion del driver I2C legado en ESP-IDF 5.5, que devuelve `ESP_FAIL` cuando el puerto compartido ya esta instalado. Se observaron 30 ciclos consecutivos, hora avanzando y temperatura estable entre 21.25 C y 21.50 C, sin errores de bus. El usuario confirmo ambos estados PASS, hora y contador avanzando, pantalla estable y ausencia de reinicios. | Agregar microSD y ejecutar `test_oled_rtc_sd`. Sincronizar posteriormente la fecha/hora, que permanece en `2000-01-01`. |
| 2026-10-16 | `test_oled_rtc_sd` | PASS | OLED SSD1306, RTC DS3231 y microSD funcionaron simultaneamente usando I2C y SPI. Se observaron 58 ciclos consecutivos sin errores ni reinicios; la hora avanzo continuamente, la temperatura se mantuvo entre 21.75 C y 22.00 C y la tarjeta reporto `7377/7378 MB`. La escritura periodica en `hwtest_combo.txt` avanzo de `writes=1` a `writes=4`. El usuario confirmo las tres indicaciones PASS, hora, contador e indicador `W` avanzando, con pantalla estable y sin reinicios. | Ejecutar integracion completa con SIM7080G mediante `test_all_no_ade`. |
| 2026-10-16 | `test_all_no_ade` | PASS con observacion de hora | Perfil integrado con ADE y metrologia omitidos. Se corrigieron dos fallos de UI: salida deterministica del splash y refresco periodico independiente de eventos de medicion. El SIM7080G recupero UART a 57600, registro LTE Cat-M1, activo el APN de Movistar, conecto MQTT y reanudo publicaciones SenML. El usuario confirmo la pantalla principal con `CEL:OK`, `MQT:OK` y `ADE:--`. OLED/ESP permanecieron estables y el supervisor reporto stacks saludables. | `AT+CCLK` aun no entrega una hora celular valida, por lo que aparece `SIN FECHA`. Ejecutar perdida y recuperacion controlada del SIM; despues validar hora RTC y backlog SD sin MQTT. |
| 2026-10-16 | `test_all_no_ade` perdida/recuperacion SIM | PASS | Al apagar manualmente el SIM, el firmware detecto la perdida MQTT y UART; la OLED cambio a `CEL:--`/`MQT:--` y el ESP32 siguio estable sin reinicio. Tras encender de nuevo el modem, recupero AT a 57600, registro LTE Cat-M1 unos 8 s despues y MQTT aproximadamente 7 s mas tarde; publico nuevamente `/conexion` y `/datos` sin reiniciar el ESP32. El usuario confirmo el retorno visual a `CEL:OK`, `MQT:OK` y `ADE:--`. | La recuperacion requiere por ahora encendido manual mediante PWRKEY. Automatizar control y politica de recuperacion sin ocultar fallas de alimentacion. |
| 2026-10-17 | `test_oled_rtc_service` | PASS parcial de servicio | El servicio detecto inicialmente el DS3231 presente, temperatura `20.50 C`, pero rechazo su fecha por `OSF=1`. Se aplico una sincronizacion UTC remota unica; la escritura fue releida, OSF paso a cero y se obtuvo `HWTEST RESULT OLED+RTC_SERVICE PASS`. Luego se retiro el epoch de prueba, se recompilo y el arranque limpio dio `source=RTC`, `state=OK`, `ready=1`, `rtc_valid=1`, `failures=0`, temperatura `20.75 C` y hora Bogota creciente. | Mantener el portabateria vacio. Abrir primero la ruta de carga del modulo DS3231 y solo despues repetir la retencion con una CR2032 nueva. |
| 2026-10-18 | `test_oled_rtc_service` sincronizacion y retencion | PASS controlado | Se cargo una imagen de prueba con epoch remoto temporal. Con el DS3231 conectado, el servicio escribio y releyo la hora: `state=OK`, `ready=1`, `rtc_valid=1`, `OSF=0`, `EOSC=0` y fuente inicial `REMOTE`. Antes de apagar se registro `2026-10-18 18:00:49` Bogota. Tras 7 min 16 s sin alimentacion principal, el reinicio entrego `source=RTC` y `2026-10-18 18:08:05`, con hora creciente, temperatura de 20.75 C y cero fallas; no se reaplico la fuente remota. | La prueba uso brevemente una CR2032 nueva medida inicialmente en 3.29 V. Retirarla con la PCB apagada y volver a medirla. No dejarla instalada de forma permanente mientras siga presente la ruta de carga de 3.074 V. |
| 2026-10-18 | Automatizacion SIM7080G/PWRKEY y recuperacion integrada | PASS | Se agrego un controlador estatico exclusivo para GPIO4: LOW inactivo, pulso HIGH de 3 s solo despues de tres ciclos reales sin respuesta AT, espera de arranque de 30 s, maximo de dos pulsos y cooldown de 60 s. Se separaron `SIM_OFF`, `SIM_NO_AT`, `CELL_NO_SERVICE`, `PDP_DOWN` y `MQTT_DOWN`, sin accionar PWRKEY cuando AT si responde. Compilaron `test_sim_pwrkey`, `test_system_contracts` y `test_all_no_ade`. Con la PCB activa se apago manualmente solo el SIM: se detecto la perdida MQTT/AT, se completaron tres pruebas UART, GPIO4 genero un unico pulso, el modem recupero AT a 57600, LTE Cat-M1, PDP y MQTT. La telemetria se guardo en SD durante la caida y despues se drenaron completamente las colas de alertas y telemetria. El uptime continuo de aproximadamente 1009 s a 1124 s, sin reinicio del ESP32. El usuario confirmo que OLED regreso a `CEL:OK`/`MQT:OK` sin parpadeos. | PASS completo. La CR2032 se retiro y conservo 3.29 V despues de la prueba controlada. |
| 2026-10-18 | Sincronizacion remota por epoch MQTT | PASS | Se retiro la configuracion LWT rechazada por este firmware SIM7080G y se dejo la presencia a `ultima_conexion`/expiracion del backend. Se cargo `test_all_no_ade` y se publico `sync_time:1792368242` en el topic de comandos: la PCB lo recibio, valido y reporto `TIME_SYNCED`; `time_service` confirmo fuente `REMOTE` y aplico la misma ruta que actualiza ESP32 y DS3231. Una segunda orden `sync_time:0` fue rechazada y la publicacion normal continuo. `test_system_contracts` y `test_all_no_ade` compilaron correctamente. La sintaxis Python compilo y una prueba SQLite en memoria confirmo que un `last_seen` de 6 min vence con umbral de 5 min. El usuario confirmo que la OLED muestra correctamente la fecha y hora de Bogota. | PASS de banco cerrado. El venv local requiere reinstalar `requirements.txt` para pruebas de importacion completas del router (`python-jose` no esta instalado). |
| 2026-10-18 | Backend Railway y presencia por `last_seen` | PASS | Se desplego el commit `ace37e1` con PostgreSQL y MQTT conectados. Se fijaron `DEBUG=false` y `OFFLINE_UMBRAL_MIN=5`. Con ultima lectura a las 19:35 se apago completamente la PCB; tras superar el umbral y refrescar la vista, el medidor cambio de `Online` a `Offline`. Al encender la PCB, el SIM7080G arranco automaticamente mediante PWRKEY, la OLED recupero `CEL:OK`/`MQT:OK` y, tras una nueva publicacion, el frontend regreso a `Online` con lectura de las 19:44. | Presencia sin LWT y recuperacion automatica aprobadas. |
| 2026-10-18 | RTC despues del ciclo de alimentacion integrado | PASS con recuperacion | Despues de apagar y encender la PCB, la OLED mostro inicialmente `SIN FECHA`, `TIME:FAIL` y `RTC:FAIL`. El comando remoto de sincronizacion activo `TIME:ON`; posteriormente el RTC cambio a `OK` y la fuente de tiempo paso a `REMOTE`, confirmando recepcion del epoch, escritura del DS3231 y recuperacion independiente del servicio. | Repetir una retencion mas larga en la proxima prueba continua y confirmar que el siguiente arranque use fuente `RTC` sin necesitar sincronizacion remota. |
| 2026-10-18 | Frontend: refresco automatico y zona horaria | PASS | Se desplego el commit `71bcaa6`. Tras recargar el bundle, la vista del medidor actualizo automaticamente la ultima lectura de 20:10 a 20:11 sin pulsar el boton y presento correctamente la hora de Bogota. El sondeo de datos queda en 30 s y el historico en 60 s, sin recargar la pagina completa. | Vigilar durante las pruebas de 2 h y 24 h que el sondeo no produzca errores visibles ni crecimiento anormal de recursos. |
| 2026-10-19 | Retencion RTC de 2 horas sin SIM | PASS | Se retiro toda la alimentacion principal a las 19:21 y se mantuvo solamente la CR2032. El voltaje observado fue 3.290 V con USB y 3.285 V sin USB. Al encender aproximadamente a las 21:24, sin SIM y sin aplicar una nueva sincronizacion, la OLED mostro fecha `2026-10-19`, hora correcta, `RTC:OK`, `TIME:OK`, `HORA:RTC`, `CELL:--` y `MQTT:--`. El voltaje final fue 3.285 V. Al conectar posteriormente el SIM se recuperaron `CEL:OK` y `MQT:OK`. | Ejecutar retencion de 24 h. Confirmar por separado, antes de uso permanente, que la ruta de carga del modulo quedo fisicamente abierta y documentar la medicion del portabateria vacio. |
| 2026-10-20 | Retencion RTC de 24 horas sin sincronizacion | FAIL | Condicion inicial confirmada por el usuario: `RTC:OK`, `TIME:OK` y fecha/hora correcta. Inicio de desconexion reportado: 21:32 (fecha exacta pendiente de confirmar). Al finalizar la prueba, OLED muestra `RTC:FAIL` y `TIME:FAIL`; `CELL:--` y `MQTT:--` son esperados porque el SIM no estaba conectado. Correccion de medicion reportada: la bateria, desconectada del circuito, midio 3.290 V. La celda conserva tension, pero esta lectura no confirma que VBAT llegara al pin del DS3231 durante la prueba. Medir VBAT directamente en el modulo y registrar `ready`, `OSF`, `EOSC` y error I2C antes de sincronizar de nuevo. | Diagnosticar causa de perdida de hora, registrar fecha/hora exacta de inicio y fin, documentar tension de bateria retirada y repetir 30 min -> 2 h -> 24 h despues de corregir la causa. |
| 2026-10-20 | Preparacion de prueba ADE/mSure | PASS de compilacion; pendiente de prueba en placa | Se corrigieron las acciones del menu y el comando MQTT `calibrate`: antes llamaban a `task_calibration_start()` sobre una tarea que ya estaba iniciada y no encolaban la solicitud; ahora publican `CALIB_REQUEST_FULL` en la cola. `METER_VNOM_V` se alinea a 120 V para el montaje residencial previsto. Compilo `esp32s3_devkitc_1`. | Antes de energizar la carga, confirmar con la referencia una tension nominal compatible. En placa, solicitar `calibrate` y confirmar `mSure done`, `CAL` con `0x0E`, luego reiniciar sin borrar NVS y confirmar `calibration loaded from NVS`. Comparar lecturas contra la referencia antes/despues. |

## Repeticion de retencion RTC (reporte del 2026-10-21)

- Inicio reportado: 21:17, con 3.290 V tanto con USB como sin USB. Por el
  contexto corresponde al dia anterior; no se dispone de un log de inicio.
- Lectura final antes de reconectar: **3.267 V en el portabateria**, con la
  bateria instalada. Correccion final expresa del usuario: el digito inicial
  era 3, no 2. Sustituye los reportes preliminares de 2.67 V y 2.267 V para
  esta repeticion, no la evidencia de pruebas previas.
- La hora de lectura final reportada previamente fue 21:12 del dia 21; si el
  inicio fue el dia 20, el intervalo hasta esa lectura fue 23 h 55 min. La hora
  exacta de reconexion no quedo registrada. Reportar aproximadamente 24 h, no
  una duracion exacta de 24 h.
- Tras reconectar la alimentacion principal y antes de conectar el SIM7080G,
  el usuario observo `RTC:OK`, `TIME:OK`, hora correcta 21:14 y `HORA:RTC`.
  Resultado: **PASS funcional de retencion en esta repeticion, por observacion
  del usuario**. No se registraron `OSF`, `EOSC`, error I2C ni una comparacion
  de segundos contra una referencia; no se cuantifico deriva del reloj.
- El SIM7080G se conecto despues de comprobar la fuente RTC. El usuario
  confirmo posteriormente `CEL:OK`, `MQT:OK` y `SD:OK` en pantalla. Despues
  reporto que la ultima lectura en la plataforma avanza aproximadamente cada
  minuto. La captura suministrada muestra `SM-348518435344` Online, ultima
  lectura y heartbeat a las 21:25, con reloj del PC a las 21:26 del 21 de
  octubre. Resultado: PASS funcional observado de actualizacion de plataforma;
  no se verificaron directamente MQTT/API/BD ni periodicidad exacta. Queda
  pendiente conciliar claves SD-BD y calcular entrega y latencia.
- Observaciones de la misma captura: variables electricas en cero, 20 alarmas
  activas y firmware mostrado `1.0.0`. Los ceros no validan metrologia; pueden
  corresponder al perfil sin ADE, pero falta confirmar la imagen ejecutada.
  Clasificar las alarmas por tipo, fecha y condicion presente antes del piloto,
  sin borrarlas para ocultar fallas. La version del encabezado proviene del
  registro del dispositivo en backend, que tiene `1.0.0` como valor por defecto;
  no demuestra por si sola la version realmente instalada. Confirmar version
  por log de arranque y payload `fw`, y verificar los campos B06-B09 antes de
  declarar activa la instrumentacion `1.1.0-obj4`.
- La diferencia corregida entre las lecturas reportadas es 0.023 V (23 mV),
  no 1.023 V. Se retira la alerta de gran caida basada en el dato equivocado.
  Estas dos lecturas no permiten estimar capacidad restante ni autonomia ni
  cuantificar la incertidumbre sin datos del instrumento. Sigue pendiente la
  comprobacion de la ruta de carga ya observada y la causa del FAIL anterior.
- Si se utiliza una CR2032, no dejarla instalada permanentemente sin confirmar
  que la ruta de carga esta fisicamente abierta. Cualquier cambio de conexiones
  o retirada de la celda se hace con la alimentacion principal desconectada.

Esta evidencia se agrega a la bitacora y no reemplaza el FAIL del 20 de
octubre. No equivale a la prueba continua del sistema completo: quedan
pendientes el cierre del respaldo electrico, el piloto de trazabilidad y las
pruebas metrologicas del ADE9153A.

## Preparacion de trazabilidad para el Objetivo 4 (2026-10-20)

Se implementaron B06-B09 en el firmware `1.1.0-obj4`, backend y plataforma:
identidad compartida entre historico/auditoria/cola SD y MQTT, timestamps de
dispositivo/servidor, radio CPSI, contadores independientes y deduplicacion.
Se agrego una proteccion para no confirmar dos veces la misma cabeza de cola
mientras la primera confirmacion esta pendiente de procesar.

Verificacion local: firmware normal compila; plataforma compila; 17 pruebas
del backend sobre SQLite en memoria pasan. Los vectores C se compilaron pero
su ejecucion en la placa esta pendiente. El firmware no se cargo y los
servicios no se desplegaron en esta revision. Los bloqueadores se cerraran
experimentalmente despues del piloto descrito en `OBJ4_TELEMETRY_VALIDATION.md`.

## Comprobacion de respaldo RTC

1. Lectura antes de retirar la alimentacion: `2000-01-01 00:02:37`.
2. Se desconecto completamente la alimentacion principal, dejando instalada la bateria del RTC.
3. Lectura despues de reconectar: `2000-01-01 00:11:01`.
4. Resultado: PASS; el reloj avanzo 8 minutos y 24 segundos sin reiniciarse.

Esta fue una comprobacion historica. La bateria retirada midio 1.394 V y el
portabateria vacio, con la PCB alimentada, midio 3.074 V: el modulo tiene una
ruta de carga. El portabateria debe permanecer vacio hasta abrir esa ruta; no
instalar otra CR2032 en el estado actual.

La fecha inicial `2000-01-01` indica que el reloj aun no se ha ajustado. Esto no
invalida las pruebas de comunicacion, oscilador o temperatura. La hora real se
puede escribir posteriormente desde el firmware o sincronizar desde la red
celular.

## Corrida basal de 12 horas con ADE y comunicaciones (2026-10-23/24)

- Ventana: `2026-10-23 21:38:50-05:00` a
  `2026-10-24 09:38:50-05:00`.
- Montaje: AC energizada con proteccion, sin carga externa, ADE/RTC/OLED/SIM
  activos y microSD desconectada.
- Resultado: **PASS de estabilidad funcional**.
- Un solo `boot_id`, cero reinicios, AC 100 %, ADE 100 % y 713 mediciones
  consecutivas (`seq=1..713`) sin huecos, duplicados ni conflictos.
- PDR de mensajes generados: 100 %. Cobertura temporal nominal: 99.03 %.
- Latencia p50/p95/p99: 14.937/15.432/21.797 s; maxima: 47.411 s.
- Vrms min/promedio/max: 120.073/120.160/120.280 V. Frecuencia:
  59.927/59.999/60.085 Hz.
- Corriente, potencia y energia permanecieron en cero durante las 713 muestras;
  no se observo creep en esta corrida.
- Se registraron dos incidentes MQTT, de aproximadamente 8 s y 108 s, ambos
  despejados. La pausa mayor produjo un intervalo de 127 s sin perdida de las
  secuencias que si fueron generadas.
- El resultado no demuestra exactitud metrologica ni clase 1; `cal_ok=false`
  obliga a ejecutar comparacion contra referencia y calibracion.

La evidencia detallada esta en
`docs/PROTOCOL_12H_ENERGIZED_NO_LOAD_2026-10-23.md`.

## Integracion ADE + microSD en SPI compartido (2026-10-24)

- La nueva microSD se alimenta a 3.3 V y comparte SPI2 con ADE9153A.
- CS independientes confirmados: microSD GPIO7 y ADE GPIO10.
- Se centralizo el propietario del bus en `drivers/spi_shared`; desaparecio el
  error `spi_bus_initialize: SPI bus already initialized`.
- Durante una prueba inicial aparecio un `ESP_ERR_TIMEOUT` de lectura SD. El
  servicio se recupero automaticamente en aproximadamente un segundo y el ADE
  continuo operativo.
- Por margen de integridad de señal en la PCB, la frecuencia SD se redujo de
  20 MHz a 4 MHz. Despues del cambio se observo una medicion de 60 s y drenado
  de `telemetry_v1.jsonl` sin nuevo timeout.
- Dictamen: **PASS de humo con mitigacion**. No se declara cierre de estabilidad
  hasta completar 60 minutos, conciliacion SD-BD y un corte de red con drenado.

## Integracion de teclado y plataforma (2026-10-24)

- Se detectaron individualmente las cuatro teclas del teclado de membrana,
  incluida pulsacion corta, larga y repetida.
- Mapa fisico cargado: tecla 4/UP GPIO2, tecla 3/DOWN GPIO1,
  tecla 2/SELECT GPIO41 y tecla 1/BACK GPIO42.
- La plataforma fue desplegada en Railway desde `origin/main`, commit
  `9fca7d1`, con frontend, backend, PostgreSQL y Mosquitto online.
- Se incorporaron ventanas historicas completas, eventos con estados
  `raised/cleared`, separacion de alarmas activas y una banda residencial de
  108-132 V que exige persistencia minima de 60 s.
- Pendiente inmediato: repetir una secuencia corta de las cuatro teclas sobre
  el firmware remapeado y ejecutar la aceptacion visual de la plataforma.

## Validacion del nuevo servicio de tiempo

1. El DS3231 presente con `OSF=1` se clasifico como hora invalida sin inventar
   timestamp y el modulo alcanzo `FAILED` despues de tres reintentos rapidos.
2. OLED y ESP32 permanecieron activos durante la falla del RTC.
3. Una sincronizacion UTC valida escribio y releyo el DS3231 antes de limpiar
   OSF; el estado regreso a `OK`.
4. El perfil final, sin epoch de prueba, reinicio con fuente `RTC` y produjo
   `PASS` de forma autonoma.
5. La evidencia de respaldo queda bloqueada hasta desactivar la ruta de carga;
   mientras tanto `RTC_BACKUP_LOST` y hora no valida son el comportamiento
   esperado y no deben afectar OLED, SD ni ESP32.
