# Protocolo P8A: estabilidad 12 h energizado y sin carga

## Objetivo

Evaluar durante al menos 12 horas la estabilidad del medidor completo con la
entrada AC energizada, sin carga externa y con microSD desconectada. La prueba
cuantifica continuidad del ESP32/ADE, presencia de la red, comunicaciones
celular/MQTT, entrega de paquetes, latencia, calidad de señal y alarmas.

Esta corrida no es una prueba de exactitud clase 1. Es una prueba funcional y
de estabilidad en vacio.

## Configuracion congelada

- Firmware: `1.1.0-obj4`, entorno `integration_no_sd`.
- Periodo de `/datos`: 60 s, QoS 0.
- Periodo de `/estado`: 5 min, QoS 1 retained.
- `/alerta`: por evento, QoS 1.
- MicroSD: desconectada y `METER_STORAGE_ENABLE=0`.
- Entrada AC: energizada con proteccion, sin carga conectada.
- RTC, OLED y SIM7080G conectados.
- No manipular, recablear ni cargar firmware durante la ventana.

## No borrar la base de datos

Los datos anteriores se conservan como evidencia. La corrida se aisla mediante:

1. instante UTC exacto de inicio y fin;
2. `boot_id` generado al arrancar;
3. secuencia `seq` de cada medicion;
4. version de firmware;
5. `uptime_ms` y causa de reset.

El informe se consulta con:

```text
GET /api/v1/salud/SM-348518435344/estabilidad?desde=<UTC>&hasta=<UTC>
```

Las alarmas de la misma ventana se consultan con:

```text
GET /api/v1/eventos/?device_id=SM-348518435344&desde=<UTC>&hasta=<UTC>&limit=500
```

## Variables registradas

| Bloque | Variables |
| --- | --- |
| Continuidad | `boot_id`, `uptime_ms`, cantidad de boots, causa de reset |
| Red AC | estado AC, `ac_loss_count`, `ac_restore_count`, eventos `AC_LOST` y `AC_RESTORED` |
| ADE9153A | estado, perdidas SPI, recuperaciones, Vrms, frecuencia, temperatura |
| Sin carga | Irms, potencia activa, aparente, reactiva y factor de potencia |
| Paquetes | generados, intentos, aceptados por modem, recibidos unicos, huecos, duplicados y PDR |
| Latencia | `ts_server - ts_device`: minimo, promedio, p50, p95, p99 y maximo |
| Celular | intentos/exitos, RSSI, RSRP, RSRQ y SINR |
| MQTT | intentos, sesiones exitosas y reconexiones |
| Alarmas | total, activas al cierre y conteo por tipo |

## Secuencia de ejecucion

1. Mantener AC desenergizada y microSD desconectada.
2. Compilar y cargar `integration_no_sd`.
3. Confirmar arranque, RTC/TIME, SIM, MQTT y ADE.
4. Registrar `boot_id`, hora UTC/local y contadores iniciales.
5. Energizar LINE/NEUTRAL con las protecciones ya verificadas, sin carga.
6. Confirmar `AC=PRESENT`, tension razonable y frecuencia cercana a 60 Hz.
7. Registrar la restauracion inicial como linea base; no contarla como caida.
8. Dejar el equipo sin intervencion durante 12 horas como minimo.
9. Consultar el informe con la ventana UTC exacta y exportar alarmas.
10. Registrar cualquier intervencion, corte deliberado o manipulacion.

## Criterios de aceptacion propuestos

| Indicador | Criterio 12 h |
| --- | --- |
| Reinicios no controlados | 0 |
| Perdidas AC posteriores a la restauracion inicial | 0 si la red fisica no cae |
| Perdidas del ADE | 0 |
| Recuperaciones del ADE | 0, salvo prueba deliberada documentada |
| PDR observado | >= 99 % |
| Paquetes | aproximadamente 720; explicar diferencias de inicio/fin |
| Alarmas criticas no explicadas | 0 |
| Reloj valido | 100 % de los mensajes posteriores a sincronizacion |
| Tension | dentro del rango definido para la instalacion; reportar min/promedio/max |
| Frecuencia | 60 Hz nominal; reportar min/promedio/max |
| Corriente/potencia sin carga | cercanas a cero y sin crecimiento espurio de energia |
| Latencia | reportar p50/p95/p99; objetivo provisional p95 <= 60 s |

Las reconexiones celulares/MQTT no se ocultan: se reportan junto con PDR y
latencia. Una reconexion aislada no invalida por si sola la corrida si no causa
reinicio ni perdida de datos, pero debe explicarse.

## Limitacion por microSD pendiente

Sin microSD se detectan huecos entre la primera y ultima secuencia recibidas de
cada `boot_id`. No se pueden demostrar paquetes perdidos antes del primero o
despues del ultimo, ni realizar reenvio tras una desconexion prolongada. Esa
conciliacion se ejecutara cuando se resuelva la interfaz microSD.

## Registro de inicio

- Inicio UTC definitivo: `2026-10-24T02:38:50Z`.
- Inicio America/Bogota definitivo: `2026-10-23 21:38:50-05:00`.
- Fin minimo programado UTC: `2026-10-24T14:38:50Z`.
- Fin minimo programado America/Bogota: `2026-10-24 09:38:50-05:00`.
- `boot_id`: `2114213152`.
- Primera secuencia recibida dentro de la ventana: `seq=1`.
- Causa de reset previa al ensayo: `usb` (`reset_reason=11`), correspondiente
  al cierre del monitor serie; no cuenta como reinicio dentro de la ventana
  definitiva, pero queda documentado como intervencion previa.
- AC ya estaba energizada al arrancar; el firmware registro una restauracion
  inicial, tomada como linea base.
- Vrms inicial: `120.078 V` (`seq=1`) y `120.082 V` (`seq=2`).
- Frecuencia inicial: `59.962 Hz` (`seq=1`) y `60.026 Hz` (`seq=2`).
- Sin carga: `Irms=0.000 A`, `P=0.00 W`, `PF=0.000`.
- Contadores al inicio: AC perdidas `0`, AC restauraciones `1` (linea base),
  ADE perdidas/recuperaciones `0/0`, celular intentos/exitos `1/1`, MQTT
  intentos/exitos `1/1`, reconexiones `0`.
- Radio inicial: RSRP `-87 dBm`, RSRQ `-13 dB`, SINR `0 dB`.
- Verificacion inicial de extremo a extremo: mediciones `seq=1` y `seq=2`
  recibidas consecutivamente, cero huecos, duplicados o conflictos; latencias
  iniciales de aproximadamente `14.71 s` y `15.20 s`.
- Ventana preliminar descartada: `02:35:49Z` a `02:38:49Z`, porque el cierre
  de COM6 provoco un reset USB. No se mezcla con el resultado de 12 horas.
- Observaciones del montaje: microSD desconectada, sin carga externa.

## Registro de cierre

- Fin UTC: `2026-10-24T14:38:50Z`.
- Fin America/Bogota: `2026-10-24 09:38:50-05:00`.
- Duracion evaluada: `12 h` exactas.
- Dictamen: **CUMPLE la prueba de estabilidad funcional sin carga, con
  observacion de una pausa temporal durante recuperacion MQTT y con la
  limitacion declarada de no disponer de microSD**.
- Consulta reproducible:
  `/api/v1/salud/SM-348518435344/estabilidad?desde=2026-10-24T02:38:50Z&hasta=2026-10-24T14:38:50Z`.

## Resultados de la corrida

| Indicador | Resultado | Dictamen |
| --- | ---: | --- |
| `boot_id` | `2114213152` durante toda la ventana | Conforme |
| Reinicios dentro de la ventana | `0` | Conforme |
| Estado AC en muestras de salud | `100 %` | Conforme |
| Perdidas/restauraciones AC | `0 / 1`; la restauracion es la linea base inicial | Conforme |
| Disponibilidad ADE en muestras de salud | `100 %` | Conforme |
| Perdidas/recuperaciones ADE | `0 / 0` | Conforme |
| Mediciones recibidas | `713`, secuencias `1..713` | Conforme |
| Huecos de secuencia | `0` | Conforme |
| Duplicados/conflictos | `0 / 0` | Conforme |
| PDR de mensajes generados | `100 %` | Conforme |
| Cobertura frente a 720 lecturas nominales | `713/720 = 99.03 %` | Conforme, muy cerca del umbral |
| Mayor intervalo entre mediciones | `127 s`, entre `seq=711` y `seq=712` | Observacion |
| Intervalo mediano/promedio | `60 s / 60.618 s` | Conforme |
| Hora valida | `713/713` mensajes | Conforme |
| Latencia p50 / p95 / p99 | `14.937 / 15.432 / 21.797 s` | Conforme |
| Latencia maxima | `47.411 s` | Conforme con objetivo provisional `< 60 s` |
| Vrms min/promedio/max | `120.073 / 120.160 / 120.280 V` | Estable |
| Desviacion estandar Vrms | `0.0257 V` | Estable |
| Frecuencia min/promedio/max | `59.927 / 59.999 / 60.085 Hz` | Estable |
| Desviacion estandar frecuencia | `0.0291 Hz` | Estable |
| Irms / potencia sin carga | `0 A / 0 W` en las 713 muestras | Conforme |
| Energia acumulada inicial/final | `0 / 0 Wh` | Sin creep observable |
| Reconexiones MQTT contabilizadas | `1` | Recuperacion automatica exitosa |
| Eventos MQTT | `2` incidentes, cada uno con `raised` y `cleared`; `0` activos al cierre | Observacion |
| Alarmas criticas | `0` | Conforme |
| RSRP min/promedio/max | `-91 / -87.137 / -84 dBm` | Aceptable |
| RSRQ min/promedio/max | `-20 / -15.130 / -13 dB` | Variable/debil |
| SINR min/promedio/max | `-14 / -1.274 / 4 dB` | Variable/debil |

### Incidentes MQTT

1. `2026-10-23 22:13:07-05:00`: desconexion reportada y despejada a las
   `22:13:15` (aproximadamente 8 s). El intervalo entre `seq=35` y `seq=36`
   fue de 70 s y no se perdio ninguna secuencia.
2. `2026-10-24 09:35:17-05:00`: desconexion reportada y despejada a las
   `09:37:05` (aproximadamente 108 s). Produjo el intervalo maximo de 127 s
   entre `seq=711` y `seq=712`. La publicacion se recupero antes del cierre.

Las cuatro filas de alarmas corresponden a las transiciones `raised` y
`cleared` de esos dos incidentes; no son cuatro desconexiones independientes.

### Interpretacion del PDR

El PDR por secuencia es `100 %`: todos los mensajes que el firmware genero
entre `seq=1` y `seq=713` llegaron una sola vez al backend. La cobertura
temporal nominal es `99.03 %` al comparar contra 720 lecturas/minuto. Durante
la pausa de 127 s el modo sin microSD no genero/persistio una lectura intermedia;
por ello la continuidad de secuencia no debe presentarse como prueba de que
existieron 720 muestras. Esta distincion evita sobreestimar el desempeno y
justifica la integracion posterior de la microSD para almacenamiento y reenvio.

Los contadores de `/estado` al final de la ventana muestran 711 mensajes porque
el ultimo snapshot de salud precede a las dos ultimas mediciones. Para el PDR
final se usa la identidad de las mediciones (`boot_id`, `seq`) y no se interpreta
esa diferencia temporal como perdida o duplicacion.

### Alcance del dictamen

Este resultado valida estabilidad, continuidad del ADE, recuperacion de
comunicaciones y comportamiento en vacio. No demuestra exactitud clase 1: el
estado de calibracion permanecio `cal_ok=false`, por lo que las pruebas con
patron de referencia y calibracion son obligatorias antes de emitir una
conclusion metrologica.

## Actualizacion posterior al cierre (24 de octubre de 2026)

Esta seccion no modifica los datos ni el dictamen de la corrida congelada de
12 horas. Registra avances posteriores que preparan la siguiente prueba.

- Se conecto la nueva microSD de 3.3 V junto con el ADE9153A sobre SPI2, con
  CS independientes: GPIO7 para SD y GPIO10 para ADE.
- Se centralizo la inicializacion del bus, eliminando el error
  `spi_bus_initialize: SPI bus already initialized`.
- Se observo un `ESP_ERR_TIMEOUT` transitorio de lectura SD que se recupero
  automaticamente en aproximadamente un segundo, sin perdida funcional del
  ADE.
- Como mitigacion de margen electrico, el reloj de la microSD se redujo de
  20 MHz a 4 MHz. La prueba de humo posterior genero la lectura de 60 s y
  dreno `telemetry_v1.jsonl` sin repetir el timeout.
- Se integro el teclado de cuatro teclas con el mapa fisico definitivo:
  tecla 4/UP en GPIO2, tecla 3/DOWN en GPIO1, tecla 2/SELECT en GPIO41 y
  tecla 1/BACK en GPIO42.
- La plataforma con los ajustes de historico, eventos, alarmas activas y
  criterio residencial de tension fue desplegada en Railway desde el commit
  `9fca7d1`.

El siguiente cierre obligatorio es una corrida integrada de 60 minutos con
todos los modulos, seguida de conciliacion de al menos 30 registros SD-BD y un
corte de comunicaciones de 10 minutos. Solo despues se inicia la prueba oficial
de 24 horas.
