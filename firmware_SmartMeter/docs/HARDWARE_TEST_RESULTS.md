# Registro de pruebas de hardware sin ADE9153A

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
