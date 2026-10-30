# Piloto integrado de estabilidad de 12 horas

Estado: **finalizado, analizado y conciliado fisicamente para telemetria**.
Fecha local de inicio: 24 de octubre de 2026.

Esta es una corrida de diagnostico con el firmware desplegado, sin reiniciar,
reflashear ni borrar la base de datos. No es una prueba de exactitud metrologica
ni sustituye la conciliacion final de la microSD.

## Ventana congelada

| Campo | Valor |
| --- | --- |
| Dispositivo | `SM-348518435344` |
| Firmware | `1.1.0-obj4` |
| Inicio UTC | `2026-10-25T02:11:30.857407Z` |
| Inicio Bogota | `2026-10-24 21:11:30.857407-05:00` |
| Fin minimo UTC | `2026-10-25T14:11:30.857407Z` |
| Fin minimo Bogota | `2026-10-25 09:11:30.857407-05:00` |
| `boot_id` inicial | `1259199348` |
| Puerto de observacion | `COM6`, 115200 bps; lectura pasiva |
| Evidencia local | `../evidence/PILOT_12H_2026-10-24/` |

Los archivos `run.json`, `serial.log`, `api_snapshots.jsonl` y
`monitor_events.jsonl` registran la captura. Se consulta la API cada cinco
minutos. El servicio de captura no envia comandos AT ni modifica la BD.
El PC debe permanecer encendido para que el log serial quede completo;
el medidor y la plataforma conservan sus propios datos si el PC se suspende.

## Hallazgos previos que no se mezclan con el resultado de las 12 horas

Entre `2026-10-24T22:13:00Z` y `2026-10-25T02:07:00Z`, el mismo boot tuvo:

| Indicador | Observado antes de la ventana |
| --- | ---: |
| Reinicios ESP32 | 0 |
| Reconexiones MQTT | 27 |
| Sesiones MQTT / intentos | 28 / 43 |
| Conexion celular / intentos | 40 / 47 |
| Muestras recibidas / secuencias esperadas entre extremos | 220 / 233 |
| Huecos recibidos | 13 |
| Latencia p95 | 267.8 s |
| RSRP minimo / mediana / maximo | -124 / -119 / -109 dBm |
| SINR minimo / mediana / maximo | -12 / -2 / 4 dB |
| Transiciones invalidas ADE contadas | 2 |

Se observaron alarmas `mqtt_disconnected`, `sim_no_at` y `pdp_down`, varias ya
resueltas. Los episodios `sim_no_at` indican que el modem dejo de responder a
comandos AT; la senal RSRP es mucho peor que en la corrida basal del 23 al 24
de octubre. Esto sugiere revisar radio/antena y alimentacion del SIM, sin
atribuir aun una causa unica. `ade_perdidas=2` cuenta transiciones de lectura
invalida; no demuestra por si solo que se haya desconectado el bus SPI.
Ademas hay ocho eventos `sim_off` de tipo `occurrence` que permanecen marcados
como activos aunque el modem volvio a publicar; son un problema de semantica
de alarmas que debe corregirse despues de congelar esta corrida.

Hubo 33 lecturas de 0 V y 0 Hz al principio del boot. La AC estuvo ausente en
parte de ese intervalo (`ac_ok` 83.33 % en muestras de estado de la ventana
previa), por lo que esas lecturas no deben etiquetarse automaticamente como
error del ADE. En el inicio del piloto, AC y ADE estan activos.

## Linea base al inicio

El estado publicado a `2026-10-25T02:11:38.728593Z` reporto:

- AC, ADE, modem y MQTT activos; hora valida.
- `data_generated_count=237`, `data_publish_attempts=303`,
  `data_publish_ok=280`, `reconexiones=27`.
- `ade_perdidas=2`, sin recuperaciones ADE contadas.
- RSRP `-123 dBm`, RSRQ `-15 dB`, SINR `-2 dB`.
- `boot_id=1259199348`, `uptime_ms=14268217`.

La medicion `seq=238` se genero en la microSD a las `02:11:53Z`; el log
confirmo `queued=1` y drenado de la cola a `0 pendientes`. La medicion
`seq=239` llego al backend con 121.189 V y 60.044 Hz. Sin carga conectada,
la corriente, potencia y energia observadas son cero.

Primer control a `02:16:31Z`: el monitor continuaba leyendo COM6 y la API;
`boot_id` seguia en `1259199348`, la ultima lectura era `seq=242`, el contador
de reconexiones permanecia en 27 y `ade_perdidas` en 2. La captura inicial de
salud tuvo un fallo DNS local transitorio, pero la segunda consulta funciono y
las mediciones siguieron llegando.

Observacion visual comunicada por el operador alrededor de `02:18Z`
(`21:18` Bogota): OLED `RTC:OK`, `SD:OK`, `CEL:OK`, `MQTT:OK`, `ADE:OK`;
indicador `NET` del SIM parpadeando, `STA` y `PWR` encendidos. Esta
observacion confirma el estado visible en ese instante, no la ausencia de
caidas anteriores o posteriores.

Los campos `cnact_state`, `smstate` y `recovery_stage` del estado pueden
describir el ultimo diagnostico de una recuperacion fallida, no necesariamente
el estado instantaneo al momento de publicar `/estado`. Para clasificar una
caida se usara el log serial de ese momento.

## Comprobaciones durante y despues

1. Mantener AC protegida, sin carga externa, y el montaje sin cambios durante
   la ventana. Anotar cualquier corte o intervencion con hora local.
2. Verificar que el ESP32 no reinicie, que ADE y RTC continuen validos y que
   la microSD genere cada `seq` sin errores persistentes de escritura/SPI.
3. Medir reconexiones de SIM/MQTT, tiempos de caida, RSRP/RSRQ/SINR,
   `AT+CEREG?`, `AT+CNACT?`, `AT+SMSTATE?`, ultimo error y etapa de recuperacion
   cuando aparezcan en el log.
4. Contar muestras SD generadas y recibidas por `device_id+boot_id+seq`.
   Reportar por separado cobertura temporal, PDR en vivo y completitud
   eventual. La aceptacion `SMPUB/OK` del modem no prueba recepcion en BD.
5. Al cerrar, exportar `smartmeter/history_v1.csv`,
   `smartmeter/telemetry_audit_v1.jsonl` y `smartmeter/events_v1.jsonl` para
   conciliacion; registrar tambien si existe `telemetry_v1.jsonl` (cola
   pendiente). Retirar la tarjeta solo despues de desenergizar completamente
   el montaje.
6. Confirmar visualmente al principio y al final fecha/hora, pantallas OLED y
   estados ADE, RTC, SD, CEL y MQTT. El teclado se prueba manualmente despues
   de la ventana; la captura pasiva no ejercita los cuatro botones.

## Criterios de cierre

La prueba se informa con resultados medidos, aunque falle. Meta para una
corrida de aceptacion posterior: 0 reinicios no controlados, 0 fallos
persistentes de ADE/SD/RTC, alrededor de 720 mediciones generadas en 12 horas,
PDR en vivo >=99 %, completitud eventual SD-BD de 100 %, 0 conflictos de
identidad y todas las caidas explicadas. Con la condicion inicial de 27
reconexiones y senal debil, este piloto se clasifica como **diagnostico** hasta
completar la observacion y conciliacion.

Consulta reproducible al terminar:

```text
GET /api/v1/salud/SM-348518435344/estabilidad?desde=2026-10-25T02:11:30Z&hasta=2026-10-25T14:11:30Z
```

El campo `limitacion` de ese endpoint aun contiene el texto fijo "Sin
microSD", aunque el firmware actual si escribe en tarjeta. No usar ese texto
como evidencia del estado fisico de la SD.

## Resultado al cierre de la ventana

El monitor termino normalmente a `2026-10-25T14:11:30.884440Z`. Se analizaron
el log serial continuo y el informe del backend para la ventana exacta
`02:11:30Z` a `14:11:30Z`, es decir, del 24 de octubre a las 21:11:30 al
25 de octubre a las 09:11:30 en Bogota.

| Indicador | Resultado | Lectura |
| --- | ---: | --- |
| `boot_id` | `1259199348` durante toda la ventana | 0 reinicios |
| Generaciones SD registradas | 720, `seq=238..957` | Sin huecos; `queued=1` y hora valida en 720/720 |
| Drenados de cola SD registrados | 720 | Cada drenado termino en 0 pendientes |
| Identidades unicas recibidas por backend | 720/720 | PDR observado 100 %; 0 duplicados y 0 conflictos |
| Cadencia de `ts_device` | mediana 60 s, maximo 61 s | 720 muestras en 12 h |
| Latencia extremo a extremo | p50 16.045 s; p95 16.747 s; p99 17.327 s | Maximo 84.110 s |
| Reconexiones MQTT nuevas | 1 (`27` a `28`) | Recuperada sin perdida de `/datos` |
| Intentos/conexiones celulares nuevos | 0/0 (`47/40` sin cambio) | Sin evidencia de reconexion celular en esta ventana |
| Perdidas ADE nuevas | 0 (`2` al inicio y al cierre) | Cinco esperas del mutex SPI; ver observaciones |
| AC en muestras de estado | 100 % presente | 0 perdidas AC nuevas |
| Voltaje medido | 120.001 / 123.673 / 126.444 V min/prom/max | Dentro de la banda operativa configurada de 108 a 132 V |
| Frecuencia medida | 59.924 / 59.998 / 60.064 Hz min/prom/max | Cercana a 60 Hz |
| Sin carga | 0 A, 0 W, 0 Wh reportados | Sin crecimiento de energia observable |
| RSRP | -126 / -120.163 / -111 dBm min/prom/max | Senal debil; no diagnostica por si sola la causa del timeout |
| Eventos recibidos por backend dentro de la ventana | 0 | No implica ausencia de incidentes locales |

Los 720 registros del backend tienen `time_valid=true`, voltaje y frecuencia
distintos de cero. La primera medicion fue `seq=238` (`02:11:40Z` del
dispositivo) y la ultima `seq=957` (`14:10:55Z`). Los contadores de `/estado`
son acumulados desde el boot iniciado antes de este piloto; por eso sus
valores finales (`data_generated_count=956`, por ejemplo) no deben leerse
como totales de la ventana.

### Incidente de comunicacion

A las `2026-10-24 21:31:47` de Bogota, el firmware inicio la publicacion de
`/estado`. Tras 15 s, `SMPUB` termino en `ESP_ERR_TIMEOUT`. El diagnostico
inmediato mostro `CEREG: 0,5`, contexto `CNACT: 0,1` y `SMSTATE: 1`; el
modem seguia registrado, con datos activos y sesion MQTT reportada como
conectada. El firmware marco localmente la sesion como desconectada y
configuro una nueva; `MQTT connected` aparecio a las `21:32:08`, unos 6 s
despues del timeout. `/estado` se publico a las `21:32:11`.

La reconexion contabilizada puede ser consecuencia del tratamiento de un
timeout de publicacion mientras la sesion seguia activa; este registro no
demuestra una caida de cobertura ni del broker. La lectura `seq=259` llego
con 84.110 s de latencia, y ninguna secuencia se perdio. Antes de cambiar
politicas de radio o alimentacion, conviene corregir la comprobacion de
`SMSTATE` posterior al timeout y repetir un ensayo corto.

### Observaciones del bus y del tiempo

- Cinco avisos `SPI mutex timeout` del ADE a las 22:57 y 23:27 del 24 de
  octubre, y a las 06:36, 08:24 y 08:25 del 25 de octubre (Bogota). El
  tiempo de espera configurado para el mutex ADE es 10 ms. Uno genero un
  fallo de lectura `TEMP_TRIM`; otro, un `AVRMS read failed` y un valor
  instantaneo `Vrms=0.00 V` durante aproximadamente un segundo. Los registros
  de un minuto correspondientes (`seq=344`, `374`, `802`, `910`, `911`)
  llegaron con tension y frecuencia validas. Ningun fallo fue persistente.
- Una lectura instantanea de temperatura de `1041.88 C` a las 06:03:54 fue
  marcada `TEMP_OK=0`; al siguiente segundo regreso a aproximadamente 25.6 C.
  Requiere revisar el tratamiento de muestras invalidas, aunque no altero la
  continuidad de las mediciones electricas almacenadas en backend.
- Dos intentos de sincronizacion `CCLK` celular no entregaron hora valida.
  El reloj existente se retuvo y 720/720 mensajes conservaron hora valida.
  No se midio deriva absoluta del RTC frente a un patron horario.

### Plataforma y limites del dictamen

El endpoint de la ventana reporta cero eventos nuevos, pero el log demuestra
el timeout MQTT, las esperas SPI y los dos fallos de resincroizacion CCLK.
Ademas, una consulta posterior de `/eventos/activos` mostro 23 alarmas
anteriores todavia activas para este dispositivo: 22 `sim_off` y una
`ac_lost`, aunque el estado actual reporta modem y AC presentes. La semantica
de limpieza de alarmas necesita correccion antes de usar ese indicador como
evidencia de disponibilidad.

El monitor local hizo 144 ciclos de consulta API. Su DNS local fallo en 62
consultas de salud y nueve de ultima medicion; esos fallos son de la captura
desde el PC y no se cuentan como caidas del medidor. El log serial siguio
continuo y el backend almaceno las 720 identidades de la ventana. El intento
automatico de guardar `backend_final.json` fallo por el mismo DNS; la consulta
exacta se repitio manualmente y produjo las cifras anteriores.

El dictamen es **aprobada la estabilidad funcional integrada observada durante
12 horas, con observaciones de bus SPI y manejo de timeout MQTT**. La
conciliacion fisica de telemetria B09 se cerro despues de leer la tarjeta;
sus resultados se detallan abajo. Esta prueba sin carga tampoco demuestra
exactitud metrologica ni autonomia de bateria; `cal_ok=false` al cierre.

## Conciliacion fisica de la microSD (25 de octubre)

La tarjeta se monto como `D:` en el PC al terminar la corrida.
Se copiaron los tres archivos presentes en `D:\smartmeter\` a
`../evidence/PILOT_12H_2026-10-24/sd_originals/` sin modificar el medio.
Los hashes SHA-256 de las copias coincidieron con los originales. La tarjeta
contiene otras corridas, por lo que la seleccion fue estrictamente
`boot_id=1259199348`, `seq=238..957`, dentro de la ventana congelada.
No habia archivo de cola pendiente `telemetry_v1.jsonl` en la tarjeta al
momento de la copia; la prueba positiva de entrega proviene de la coincidencia
de las 720 identidades con la BD, no de esa ausencia.

| Comprobacion | Resultado |
| --- | ---: |
| Identidades unicas en `telemetry_audit_v1.jsonl` | 720/720 |
| Identidades unicas en `history_v1.csv` | 720/720 |
| Identidades coincidentes en backend | 720/720 |
| Huecos, duplicados y conflictos de identidad en la seleccion | 0 |
| Diferencias de las ocho magnitudes SD frente a backend | 0 |
| Diferencias de diez campos de medida auditoria frente a historico SD | 0 |
| Completitud eventual SD-backend | 100 % |
| Entrega en <=60 s (PDR en vivo) | 719/720 = 99.861 % |
| Latencia mediana / p95 / p99 | 16.045 / 16.747 / 17.327 s |

La unica entrega posterior a 60 s fue `seq=259`: 84.110 s. Llego a la BD,
por lo que no es perdida definitiva. Las 720 entradas de auditoria tienen
`time_valid=true`. Los datos del backend fueron exportados en modo lectura a
`backend_pilot_720.json` y el resultado reproducible esta en
`reconciliation.json`. Los originales fisicos tienen estos SHA-256:

```text
telemetry_audit_v1.jsonl CB8EAB8BCD5CCC1AA0E7CD73682FE6337AAFC2FD399EE6607E05BD9A3313FC15
history_v1.csv           BEA9BE21689A8FCF647B3063094D36B63AD8B0F9747872D9CD5540348CBEDEA4
events_v1.jsonl          7F9AF4A032A2D609D11E843BC14D34EE6FCBA59A89BFFB17A763DF249F56508B
backend_pilot_720.json  7FCE9C4AA9D88B962DCF9B4FA07F0F6A101E2E9FCEA69BA5ED707482EA0AD467
```

El archivo local `events_v1.jsonl` contiene tres eventos en la ventana:
`MQTT:MODULE_READY` una vez y `TIME:TIME_SYNC_FAILED` dos veces. La consulta
`/api/v1/eventos/` filtrada por dispositivo y ventana devolvio `total=0`.
**No se declara conciliacion ni entrega de eventos**: hay que determinar si
esos eventos informativos solo se auditan localmente por diseno o si deberian
publicarse; la plataforma no debe usarse como fuente exhaustiva de este log.

### Integridad de la evidencia local

```text
serial.log          SHA-256 7F01A6D3259C35290296FEFB13CFD21BACAD7D8AC362990837F0CE84912084D6
api_snapshots.jsonl SHA-256 1032FA752AF6A0FE1EBEB4D18DA3B072CA731BEDDA58A03EA295D9775B3467D5
```

## Proxima secuencia

1. Expulsar la microSD de forma segura del PC; con el medidor completamente
   desenergizado, reinstalarla y confirmar `SD:OK` antes de la siguiente
   prueba. La copia y conciliacion `seq=238..957` ya estan cerradas.
2. Corregir los eventos `sim_off`/`ac_lost` que permanecen activos tras
   recuperacion y el tratamiento de `/estado` cuando `SMPUB` vence pero
   `SMSTATE` sigue en 1.
3. Revisar el mutex ADE de 10 ms y el descarte de muestras instantaneas
   invalidas; verificar en una corrida corta que desaparecen los avisos.
4. Ejecutar un corte controlado **solo del servicio MQTT** durante 10 min,
   si el broker esta dedicado a este prototipo, para comprobar almacenamiento
   y reenvio desde SD. Mantener AC, ESP32, ADE, RTC, SIM y SD encendidos.
   Registrar la hora exacta de inicio/restauracion y esperar al vaciado de la
   cola. Esto prueba indisponibilidad de broker, no perdida de cobertura
   celular. No retirar la antena ni cortar la AC para este ensayo.
5. Probar OLED/teclado al cierre y, por separado, la conmutacion a bateria.
   Avanzar luego a exactitud con el Fluke 43B y a la corrida oficial de 24 h.
