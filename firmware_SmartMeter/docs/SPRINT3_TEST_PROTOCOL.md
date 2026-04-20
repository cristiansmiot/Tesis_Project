# Sprint 3 - Protocolo de Pruebas (ADE9153A + SIM7080G Integrado)

## 1. Objetivo

Validar en hardware real la integracion completa de extremo a extremo:

- metrologia ADE9153A operativa (V, I, P, Q, S, PF, frecuencia),
- modem SIM7080G operativo por UART,
- registro en red celular (NB-IoT/LTE-M),
- contexto PDP activo,
- conexion MQTT y publicacion de telemetria JSON con datos reales del medidor.

## 2. Arquitectura RTOS integrada

### 2.1 Tareas activas

| Tarea                  | Core | Prioridad | Periodo       | Funcion                              |
|------------------------|------|-----------|---------------|--------------------------------------|
| `task_measurement`     | 0    | 5         | 250 ms        | Lee ADE9153A, actualiza `meter_data` |
| `task_power_quality`   | -    | 4         | 250 ms        | Monitoreo sag/swell                  |
| `task_calibration`     | -    | 3         | Bajo demanda  | Rutina mSure                         |
| `task_communication`   | 1    | 2         | 60 s          | Publica JSON via MQTT                |

### 2.2 Flujo de datos

```
ADE9153A (SPI2, 100 kHz)
   |
   v
task_measurement (core 0, 250 ms)
   - voltage_get_vrms(), current_get_irms_a(), ...
   - Deteccion AC, filtro no-carga, PF fallback
   - Integracion energia software + NVS
   |
   v  meter_data_update(&snap)   [mutex protegido]
   |
meter_data (MeterData_t compartida)
   |
   v  meter_data_get_snapshot()  [mutex protegido]
   |
task_communication (core 1, 60 s)
   - data_serializer_build_current_json()
   - mqtt_client_publish() -> SIM7080G AT+SMPUB
   |
   v
Broker MQTT (shinkansen.proxy.rlwy.net:58954)
```

### 2.3 Formato JSON publicado

```json
{
  "ts_us": 1234567890,
  "vrms": 220.123,
  "irms_a": 1.234,
  "p_w": 250.560,
  "q_var": -50.120,
  "s_va": 255.000,
  "pf": 0.9821,
  "f_hz": 60.000,
  "e_wh": 1234.567890,
  "e_soft_wh": 1000.000000,
  "e_nvs_wh": 1234.567000,
  "temp_c": 42.123,
  "temp_ok": 1,
  "ade_ok": 1,
  "ade_bus": 0,
  "ade_probe_err": 0,
  "ade_rec": 3,
  "ade_act": 2,
  "pq": 0,
  "cal": 0
}
```

### 2.4 Interpretacion rapida de campos JSON

- `q_var`: potencia reactiva en VAR. El signo depende del cuadrante reportado por el ADE y de `METER_POWER_SIGN_CORRECTION`.
- `s_va`: potencia aparente en VA. En este firmware se usa como magnitud positiva.
- `temp_ok`: `1` cuando la temperatura del ADE es valida; `0` cuando la lectura se enmascara por falla o implausibilidad.
- `ade_ok`: `1` cuando el ADE esta validado y sus datos se consideran confiables.
- `ade_bus`: indica si el probe detecto un patron tipico de bus desconectado (todo `0x00` o todo `0xFF`).
- `ade_probe_err`: ultimo codigo de error del probe ADE.
- `ade_rec`: contador de recoveries ADE exitosos.
- `ade_act`: ultima accion de recovery ADE (`0=PROBE`, `1=SOFT_INIT`, `2=SPI_REINIT`).
- `e_wh`: alias legacy del total de energia publicado; hoy equivale al acumulado persistido, no a `E_hw`.
- `e_soft_wh`: energia integrada por software desde `p_w`.
- `e_nvs_wh`: energia total persistida en NVS.
- `cal`: bitmask `CALIB_STATUS_*` (`0x01` RUNNING, `0x02` VALID, `0x04` PERSISTED, `0x08` LOADED, `0x80` ERROR).

Notas practicas:

- Que `q_var` se vuelva mas negativa con la carga no implica por si solo una falla; indica crecimiento de la componente reactiva segun la convencion actual.
- Que `s_va` aumente en positivo con la carga es el comportamiento esperado.
- Si `temp_ok=0` o `ade_ok=0`, el firmware esta enmascarando datos del ADE hasta reconfirmar que el chip volvio a un estado valido.
- Si la OLED no esta conectada, los logs `oled_driver: OLED init commands failed` son esperados y no invalidan Sprint 3.
- Si `cal=0`, la metrologia puede estar funcionando con constantes por defecto, pero no hay estado mSure valido o cargado reportado.
## 3. Flujo AT implementado en firmware

Secuencia completa en codigo:

1. `AT`
2. `ATE0`
3. `AT+CMEE=2`
4. `AT+IFC=0,0`
5. `AT+CPIN?`
6. `AT+CSQ`
7. `AT+CNMP=2`
8. `AT+CSCLK=0` / `AT+CPSMS=0` / `AT+CEDRXS=0` (desactiva ahorro de energia)
9. `AT+CEREG?` (reintento hasta registro, timeout 120 s)
10. `AT+CGATT=1` (si no esta attached)
11. `AT+CGDCONT=1,"IP","<APN>"` (contexto PDP)
12. `AT+CNCFG=0,1,"<APN>"` (perfil app network para `CNACT`)
13. `AT+CNACT=0,1`
14. `AT+CNACT?`
15. `AT+SMDISC` (limpieza de sesion previa)
16. `AT+SMCONF="URL","<host>",<port>`
17. `AT+SMCONF="CLIENTID","<client_id>"`
18. `AT+SMCONF="USERNAME","<user>"`
19. `AT+SMCONF="PASSWORD","<pass>"`
20. `AT+SMCONF="KEEPTIME",60`
21. `AT+SMCONF="CLEANSS",1`
22. `AT+SMCONF="QOS",0`
23. `AT+SMCONN`
24. `AT+SMSTATE?`
25. `AT` (probe rapido pre-publish)
26. `AT+SMPUB="<topic>",<len>,<qos>,<retain>`

## 4. Criterios de respuesta esperada (red)

- `AT` -> `OK`
- `AT+CPIN?` -> `+CPIN: READY`
- `AT+CSQ` -> `+CSQ: <rssi>,<ber>` (rssi entre 1 y 31 es util)
- `AT+CEREG?` -> `+CEREG: <n>,1` o `+CEREG: <n>,5`
- `AT+CNACT?` -> al menos un contexto activo con IP (`+CNACT: 0,1,"x.x.x.x"`)

Notas:

- Si `CEREG` devuelve `0,2` o `0,3`, aun no hay registro valido.
- Si `CSQ` devuelve `99,99`, no hay calidad de senal util.

## 5. Criterios de respuesta esperada (MQTT)

- `AT+SMCONN` -> `OK`
- `AT+SMSTATE?` -> `+SMSTATE: 1`
- `AT+SMPUB=...` -> prompt `>` y luego `OK` tras payload

## 6. Preparacion previa

1. SIM activa con datos (APN correcto: `internet.movistar.com.co`).
2. Antena conectada al SIM7080G.
3. Eval board ADE9153A conectada por SPI (MOSI=11, MISO=13, SCLK=12, CS=10, RST=14).
4. Verificar pines UART en `config/board_config.h` (TX=16, RX=17).
5. Verificar APN y broker en `config/meter_config.h`.
6. Confirmar `METER_SIM7080G_FOCUS_MODE = 0` en `config/meter_config.h`.
7. Compilar y flashear firmware.

### 6.1 Modo SIM-first (solo para depuracion AT aislada)

Si ADE9153A no esta conectado o esta inestable, activar temporalmente:

- `METER_SIM7080G_FOCUS_MODE = 1` en `config/meter_config.h`

Comportamiento en este modo:

- se omite inicializacion ADE/metrologia,
- arranca solo `task_communication`,
- los campos `vrms`, `irms_a`, `p_w`, etc. seran 0.0 en el JSON.

**Para pruebas de integracion completa, METER_SIM7080G_FOCUS_MODE debe ser 0.**

## 7. Prueba A - Arranque integrado (ADE + SIM7080G)

1. Conectar eval board ADE9153A + SIM7080G + antena + SIM.
2. Encender equipo.
3. Abrir monitor serie.
4. Verificar secuencia de arranque en logs:

```
[main] firmware_smartmeter starting - Sprint 3 (ADE9153A + SIM7080G integrado)
[main] ADE9153A init attempt 1/3
[main] ADE9153A detected OK
[task_manager] task_measurement started on core 0
[task_manager] task_communication started on core 1
```

5. Confirmar que NO aparezca `ADE9153A not responding`.
6. Confirmar que NO aparezca `critical fault - restarting`.

Criterio PASS:

- ADE9153A detectado y DSP arrancado.
- Las 4 tareas creadas sin error.
- No hay restart loop.

## 8. Prueba B - Registro de red

1. Esperar a que `task_communication` intente la conexion.
2. Verificar en logs que no aparezca `network recovery failed`.
3. Confirmar secuencia:

```
[sim7080g_init] SIM7080G responded to AT @57600
[sim7080g_init] SIM7080G modem ready (baud=57600)
[mqtt_client] MQTT profile configured (SMCONF OK, pending SMCONN)
[mqtt_client] MQTT connected
```

Criterio PASS:

- El modulo llega a estado de red y PDP sin timeout.
- `SMSTATE=1` confirmado.

## 9. Prueba C - Telemetria con datos reales

Prerequisito: AC presente en la linea monitorizada.

1. Aplicar carga conocida (ej. bombilla 100 W) a la linea medida.
2. Esperar al menos 2 ciclos de publicacion (120 s).
3. Verificar en broker que el JSON contiene valores reales, no ceros:

```
vrms > 100.0     (tipicamente ~120V o ~220V segun region)
irms_a > 0.05    (corriente de carga)
p_w > 10.0       (potencia de carga)
f_hz > 45.0      (frecuencia de linea, 50 o 60 Hz)
pf > 0.5         (factor de potencia de la carga)
ts_us creciente  (timestamp cambia cada publicacion)
```

4. Verificar coherencia electrica:
   - `s_va >= p_w` (potencia aparente >= potencia activa)
   - `|pf| <= 1.0`
   - `p_w` aproximadamente igual a `vrms * irms_a * pf`

Criterio PASS:

- JSON valido con campos numericos no-cero.
- Valores coherentes con la carga aplicada.
- `ts_us` creciente entre publicaciones.

## 10. Prueba D - Telemetria sin carga (no-load)

1. Desconectar toda carga de la linea (solo voltaje presente).
2. Esperar 2 publicaciones.
3. Verificar en JSON:

```
vrms > 100.0     (voltaje presente)
irms_a < 0.010   (filtro no-carga activo)
p_w = 0.000      (limpiado por filtro)
f_hz > 45.0      (frecuencia valida)
```

Criterio PASS:

- Corriente y potencia limpiadas a cero por filtro no-carga.
- Voltaje y frecuencia siguen reportando valores reales.

## 11. Prueba E - Telemetria sin AC

1. Desconectar AC completamente de la entrada del medidor.
2. Esperar 2 publicaciones.
3. Verificar en JSON:

```
vrms = 0.000
irms_a = 0.000
p_w = 0.000
f_hz = 0.000
pf = 0.0000
```

Criterio PASS:

- Todos los campos electricos en cero (deteccion AC ausente).
- El timestamp `ts_us` sigue creciendo (el sistema sigue operativo).

## 12. Prueba F - Recuperacion de red

1. Con MQTT ya conectado y publicando datos reales.
2. Apagar fisicamente el SIM7080G por 15-20 s.
3. Verificar en logs (dentro de ~15 s gracias al heartbeat):
   - `heartbeat: MQTT session lost - triggering recovery` o
   - `pre-publish AT probe failed - modem not responding`
4. Encender nuevamente el SIM7080G.
5. Verificar secuencia de recuperacion:

```
[sim7080g_init] SIM7080G responded to AT @57600
[sim7080g_init] SIM7080G modem ready (baud=57600)
[mqtt_client] SMDISC recovery ...
[mqtt_client] MQTT profile configured (SMCONF OK, pending SMCONN)
[mqtt_client] MQTT connected
[task_communication] telemetry published topic=medidor/ESP32-001/datos
```

Criterio PASS:

- Deteccion de perdida en menos de 20 s (heartbeat cada 15 s).
- Recupera publicacion sin resetear el ESP32.
- Los datos post-recovery contienen mediciones reales (no ceros si hay AC).

## 13. Prueba G - Estabilidad prolongada (10 min)

1. Arranque completo con carga AC estable.
2. Dejar operando 10 minutos minimo.
3. Verificar:
   - Al menos 10 publicaciones consecutivas sin error.
   - No hay `publish failed` ni `mqtt recover` inesperados.
   - Valores de vrms/irms_a/p_w estables (variacion < 5%).
   - Energia `e_wh` / `e_nvs_wh` incrementandose.

Criterio PASS:

- 10+ publicaciones sin interrupcion.
- Mediciones estables y energia acumulandose.

## 14. Matriz rapida de pruebas

| # | Prueba | Resultado |
|---|--------|-----------|
| A | Arranque integrado ADE + SIM7080G | PASS / FAIL |
| B | Registro de red y conexion MQTT | PASS / FAIL |
| C | Telemetria con datos reales (con carga) | PASS / FAIL |
| D | Telemetria sin carga (no-load filter) | PASS / FAIL |
| E | Telemetria sin AC (deteccion ausencia) | PASS / FAIL |
| F | Recuperacion tras power-cycle SIM7080G | PASS / FAIL |
| G | Estabilidad 10 min continuo | PASS / FAIL |
| H | Recovery ADE9153A en runtime | PASS / FAIL |

## 14.1 Resultado observado en pruebas reales (2026-03-14)

| # | Prueba | Estado observado |
|---|--------|------------------|
| A | Arranque integrado ADE + SIM7080G | PASS |
| B | Registro de red y conexion MQTT | PASS |
| C | Telemetria con datos reales (con carga) | PASS |
| D | Telemetria sin carga (no-load filter) | PASS |
| E | Telemetria sin AC | PASS |
| F | Recuperacion tras power-cycle SIM7080G | PASS |
| G | Estabilidad 10 min continuo | PASS |
| H | Recovery ADE9153A en runtime tras power-cycle del chip | PASS |

Nota:

- SIM7080G quedo validado tambien cuando el modulo se apaga y se vuelve a encender durante la prueba; la ESP32 mantiene su operacion y el enlace MQTT se recupera.
- El recovery runtime del ADE9153A quedo validado: ante perdida de alimentacion o respuesta invalida, la telemetria se enmascara (`ade_ok=0`, `temp_ok=0`) y vuelve a mediciones reales despues de `SPI_REINIT`, sin reiniciar toda la ESP32.
- Los logs `oled_driver: OLED init commands failed` son esperados mientras la pantalla no este conectada.
- Como prueba opcional de cierre extra, queda por validar el caso de arranque en frio con el ADE ausente desde el primer segundo de power-up.

## 15. Comandos AT de diagnostico manual (opcional)

Usar solo si tienes consola AT directa al SIM7080G:

1. `AT`
2. `ATI`
3. `AT+CPIN?`
4. `AT+CSQ`
5. `AT+CEREG?`
6. `AT+CGATT?`
7. `AT+CNACT?`
8. `AT+SMSTATE?`

## 16. Evidencia minima a guardar

- Captura de logs de arranque completo (ADE + SIM7080G).
- Captura con `MQTT connected`.
- Captura con `telemetry published` mostrando datos reales.
- Screenshot del broker recibiendo JSON con `vrms > 0` y `p_w > 0`.
- JSON completo de al menos una publicacion con carga.
- Captura del recovery ADE con ADE recovery completed action=2 o equivalente.
- Resultado PASS/FAIL por cada prueba (A-H).

## 17. Troubleshooting rapido (ADE9153A)

Caso: el ADE pierde alimentacion o entrega patron SPI invalido en runtime.

1. Verificar pines SPI en `config/board_config.h`:
   - MOSI=11, MISO=13, SCLK=12, CS=10, RST=14.
2. Confirmar alimentacion del ADE9153A y GND comun.
3. Durante la falla, el comportamiento esperado ahora es:
   - `ADE_OK=0`
   - `TEMP_OK=0`
   - `Vrms`, `IrmsA`, `P` y `F` en cero
   - logs `ADE recovery requested`
4. El firmware intenta `SOFT_INIT` y, si falla, escala a `SPI_REINIT`.
5. Si el ADE sigue sin alimentacion, el sistema debe permanecer vivo en `AC=LOST` sin publicar valores absurdos.
6. Cuando el ADE vuelve, el log esperado es `ADE recovery completed action=2` y luego una rampa corta hasta recuperar metrologia estable.
7. Si la OLED no esta conectada, ignorar `oled_driver: OLED init commands failed`; no afecta la validacion del recovery ADE.

Caso: JSON con `vrms=0.000` cuando hay AC presente.

1. Verificar umbral de deteccion AC: `METER_AC_PRESENT_VRMS_RAW_ON_MIN = 100000`.
2. Confirmar conexiones del divisor resistivo de voltaje.
3. Revisar logs de `task_measurement` para ver raw VRMS.
4. Verificar que la calibracion este cargada (`cal` en JSON debe tener bit VALID).

## 18. Troubleshooting rapido (AT timeout)

Caso: aparecen reintentos `cmd=AT err=ESP_ERR_TIMEOUT`.

1. Verificar que el log muestre los pines reales en uso:
   - `sim7080g_hal: HAL ready uart=... tx=... rx=... baud=...`
2. Confirmar cruce correcto:
   - TX ESP32 -> RX SIM7080G
   - RX ESP32 -> TX SIM7080G
   - GND comun obligatorio
3. Confirmar alimentacion del modulo:
   - SIM7080G requiere fuente estable (picos de corriente ~2A).
4. Confirmar que el modem esta encendido (PWRKEY del modulo).
5. Reintentar con antena conectada y SIM insertada.

Si persiste:

- Probar consola AT directa (USB-TTL al SIM7080G) con `AT`.
- Si no responde directo, el problema es hardware/alimentacion/encendido del modulo.

## 19. Troubleshooting rapido (CNACT fail)

Caso: `AT` responde, pero falla activacion PDP (`AT+CNACT...`).

1. Revisar lineas de log con respuesta textual:
   - `CGDCONT failed. resp=...`
   - `CNACT cid=0 failed. resp=...`
   - `PS not attached...` / `CGATT attach failed...`
2. Si aparece `+CME ERROR`:
   - validar APN (`METER_SIM7080G_APN`) segun operador real de la SIM.
   - confirmar que la SIM tenga plan de datos activo.
3. Si `AT+CNACT=0,1` devuelve `operation failed`:
   - consultar `AT+CNACT?`; en varios firmwares el contexto puede quedar activo aun con ese error.
4. Si no logra `CGATT: 1`:
   - la red aun no adjunta datos (cobertura/tecnologia/operator profile).

## 20. Notas clave de la version actual

1. `MQTT profile configured` **no** significa conectado al broker.
   - Solo confirma que `SMCONF` se aplico.
   - Conexion real: `MQTT connected` (internamente `SMSTATE=1`).
2. El firmware implementa **heartbeat cada 15 s** entre publicaciones.
   - Si el modem muere, se detecta en ~15 s (no espera 60 s al siguiente publish).
   - Log esperado: `heartbeat: MQTT session lost - triggering recovery`.
3. Antes de cada publish se hace **AT probe rapido** (2 s timeout).
   - Si falla, se salta directamente a recovery sin gastar 45 s en retries SMPUB.
   - Log esperado: `pre-publish AT probe failed - modem not responding`.
4. En recovery, el firmware siempre ejecuta `AT+SMDISC` y reconfigura perfil MQTT.
   - Esto previene errores `SMCONN ERROR` por estado MQTT interno corrupto tras power-cycle.
5. El health-check del modem se fuerza a re-probe tras 2 fallos consecutivos (antes eran 4).
6. Tiempos de recovery configurados:
   - `METER_COMM_RECOVERY_RETRY_MS = 5000 ms`
   - `METER_COMM_HEARTBEAT_INTERVAL_MS = 15000 ms`
   - `METER_MQTT_SMCONN_TIMEOUT_MS = 30000 ms`
   - `METER_MQTT_SMCONN_POSTWAIT_MS = 10000 ms`

## 21. Configuracion relevante (meter_config.h)

| Parametro | Valor | Funcion |
|---|---|---|
| `METER_SIM7080G_FOCUS_MODE` | 0 | Integracion completa (ADE + SIM) |
| `METER_MEASUREMENT_PERIOD_MS` | 250 | Ciclo de lectura ADE9153A |
| `METER_COMM_PUBLISH_PERIOD_MS` | 60000 | Intervalo de publicacion MQTT |
| `METER_COMM_HEARTBEAT_INTERVAL_MS` | 15000 | Heartbeat entre publishes |
| `METER_COMM_RECOVERY_RETRY_MS` | 5000 | Delay rapido en recovery |
| `METER_NO_LOAD_IRMS_A_MAX_A` | 0.010 | Umbral filtro no-carga |
| `METER_NO_LOAD_ACTIVE_POWER_W_MAX` | 1.00 | Umbral potencia no-carga |
| `METER_AC_PRESENT_VRMS_RAW_ON_MIN` | 100000 | Umbral deteccion AC |
| `METER_POWER_SIGN_CORRECTION` | -1.0 | Correccion polaridad CT |
| `METER_MQTT_KEEPALIVE_S` | 60 | MQTT keep-alive |
| `METER_ENERGY_NVS_SAVE_PERIOD_MS` | 5000 | Checkpoint energia NVS |



