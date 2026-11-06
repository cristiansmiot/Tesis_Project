# Versionado y trazabilidad de las pruebas del firmware

## Regla para conservar versiones

Cada versión debe corresponder a un cambio concreto y tener una evidencia de
prueba asociada. Usaremos Git como historial principal:

1. Antes de probar, registrar el commit de partida, el perfil de compilación,
   los cambios locales, la versión mostrada por el equipo y la configuración
   física relevante.
2. Mantener cada corrección en un cambio acotado y revisable. No mezclar en el
   mismo commit firmware, backend, documentación y archivos de evidencia si
   pueden revisarse por separado.
3. No llamar “estable” a una versión hasta que complete el protocolo definido.
   Las versiones que fallan se conservan como evidencia diagnóstica y no se
   borran del historial.
4. Crear una etiqueta de Git para una versión aceptada después de registrar el
   commit que realmente se probó. La etiqueta debe apuntar al mismo código que
   quedó instalado en la placa.
5. Guardar por prueba el registro serie, la carpeta de respaldo de la microSD,
   las horas de inicio y fin, reinicios, condiciones de alimentación y
   resultado. Anotar si las lecturas fueron verificadas también en el backend.

El campo `METER_FW_VERSION` identifica el firmware en la pantalla y la
telemetría. Los perfiles experimentales deben asignar una versión explícita en
`platformio.ini` e imprimirla al inicio. El número de versión identifica el
binario; el commit y el SHA-256 identifican el código y los archivos usados
para producirlo.

## Captura previa a la corrección MQTT

La prueba aislada del 28 de octubre de 2026 se conserva en
`evidence/versions/2026-10-28_mqtt-isolation-before-recovery/`. Es una versión
diagnóstica fallida: obtuvo 24 publicaciones exitosas y después registró un
timeout de publicación, pérdida temporal de respuesta AT y reintentos
`SMCONN` rechazados con `operation not allowed`, aun con registro celular y
PDP activos.

El commit `842a3fd` es el último commit de Git que precedía esta corrección,
pero el firmware diagnóstico se compiló desde un árbol de trabajo que contenía
cambios locales. Por eso el manifiesto conserva copias exactas de los archivos
relevantes y sus SHA-256; el hash del commit por sí solo no identifica ese
binario. No se presenta esta captura como versión estable.

## Versión experimental MQTT `1.1.0-mqtt-r1`

La versión experimental `1.1.0-mqtt-r1` aborda el caso donde `SMCONN` devuelve
`operation not allowed` y `SMSTATE` indica desconectado. El firmware cicla PDP
como recuperación, verifica que el contexto realmente haya bajado antes de
reactivarlo y espera al siguiente intento programado para reconectar MQTT. El
ciclo se limita a uno por minuto.

La compilación del perfil `test_sim_mqtt` pasó el 28 de octubre de 2026 con
PlatformIO Core 6.1.19, plataforma Espressif 32 6.12.0 y ESP-IDF 5.5.0. El
binario resultante tiene SHA-256
`AE83445857C65CA10BD27C288B46D05E7D6B64019CD68B9F9024079A788D6272`, usa
78 428 bytes de RAM de 327 680 y 500 397 bytes de flash de 1 048 576.

Este binario se compiló desde un árbol de trabajo con modificaciones locales;
`842a3fd` es su commit base, no el commit exacto de este binario. No se guardó
el binario en el repositorio porque incorpora la configuración de conexión del
perfil. La compilación pasó, pero la prueba MQTT aislada del 29 de octubre no
cumplió el criterio de estabilidad: después de un timeout y la pérdida de
respuesta AT, un ciclo PDP se completó, aunque `SMCONN` siguió respondiendo
`operation not allowed` y no hubo reconexión MQTT confirmada antes del corte.
R1 queda como versión experimental fallida, no estable. Antes de etiquetar o
distribuir una versión aceptada, se debe registrar un commit que contenga
exactamente el código que se volvió a compilar y probar.

La captura de los archivos relevantes y sus huellas está en
`evidence/versions/2026-10-28_mqtt-isolation-r1-build/`. El informe de la
prueba física está en `docs/PRUEBA_MQTT_AISLADO_R1_2026-10-29.md` y el registro
serie completo en `evidence/mqtt-isolation-r1-20261029.log`. Como el binario se
compiló desde el árbol de trabajo completo, la captura ayuda a revisar los
cambios MQTT, pero no sustituye un commit limpio para reproducir exactamente
el binario.

## Perfil de diagnóstico de red `1.1.0-netdiag-r1`

El perfil `test_sim_network_long` fue creado para investigar el patrón de
sesiones MQTT de aproximadamente 28 minutos seguidas por unos 33 minutos sin
conexiones TCP observadas por el broker. Desactiva las pruebas de ADE, SD y
MQTT, y registra cada 15 s el estado de red/señal; cada 5 min registra
`CEREG`, `COPS`, `CNACT` y `CPSI`. Ante una caída toma una captura AT antes de
iniciar la recuperación. Su objetivo es distinguir fallos de registro o PDP
del comportamiento del cliente MQTT; no prueba conectividad TCP a Railway.

La compilación de `test_sim_network_long` pasó el 29 de octubre de 2026 con
PlatformIO Core 6.1.19, plataforma Espressif 32 6.12.0 y ESP-IDF 5.5.0. El
binario tiene SHA-256
`B100184FDEF27B8A1F2FDE55F910673C881A1B06AA397DEC82D65E96014F0AF2`, usa
78 428 bytes de RAM de 327 680 y 499 349 bytes de flash de 1 048 576. La
compilación no implica que el perfil se haya cargado ni que la prueba física
haya comenzado. La captura de fuentes y manifiesto está en
`evidence/versions/2026-10-29_netdiag-r1-build/`.

La hipótesis de roaming se mantiene como hipótesis: `CEREG=5` significa que el
módem está registrado en roaming, pero no identifica por sí solo la causa de
una interrupción. Para contrastarla se requiere observar junto con registro y
PDP el operador/PLMN, tecnología y celda durante una corrida prolongada.

La corrida física del perfil se completó por USB y sin AC el 29 de octubre. Se
excluyó del análisis válido el primer intervalo porque el módem se apagó tras
una pulsación manual de PWRKEY. Desde `NETDIAG READY session=2` (11:29:34.341)
hasta el último muestreo (12:40:07.334) transcurrieron 70 min 33,07 s: 281
muestras CSQ, 15 capturas de cada estado CEREG/COPS/CNACT/CPSI y cero caídas
espontáneas de red o PDP. El resultado reduce la probabilidad de una pérdida
periódica de registro/PDP, pero no prueba acceso TCP al proxy ni descarta las
capas TCP, proxy o cliente MQTT. El protocolo y análisis están en
`docs/PRUEBA_RED_CELULAR_SIN_MQTT_2026-10-29.md`; el log original permanece en
`evidence/sim-network-long-20261029.log`.

## Perfil de diagnóstico TCP `1.1.0-tcpdiag-r1`

El perfil `test_sim_tcp` está diseñado para abrir y cerrar un socket TCP al
proxy público del broker una vez por minuto, sin enviar paquetes MQTT ni
telemetría. Registra la respuesta de apertura, el estado de socket, el cierre,
su duración y los estados celulares ante fallos. Busca distinguir un problema
de DNS/ruta/transporte TCP o proxy de un problema del cliente MQTT AT del
módem. No altera el broker ni la base de datos.

La compilación y carga del perfil pasaron el 29 de octubre de 2026 en COM6; el
SHA-256 del binario es
`2619AEDB04FA3486ACBB9FE80DE99FFDB52D2975DEE2E9AA76B4D3CD928B0BEF`. El
intento TCP fue rechazado por el módem con `+CME ERROR: operation not allowed`
mientras CEREG y PDP seguían activos. El registro parcial no incluyó las
consultas de estado previas a la primera conexión, por lo que esta versión no
permite atribuir el rechazo. Se conserva como diagnóstico fallido; fuentes y
binario están en `evidence/versions/2026-10-29_tcpdiag-r1-build/`.

## Perfil de diagnóstico TCP `1.1.0-tcpdiag-r2`

R2 conserva la prueba TCP de R1 y añade consultas de capacidades `CAOPEN`,
estado de sockets `CASTATE`, estado del cliente MQTT `SMSTATE` y configuración
de sockets `CACFG`, tanto al inicio como ante el rechazo de apertura. Esto
permite diferenciar un socket anterior ocupado o una incompatibilidad de
estado del módem de un fallo de DNS/ruta TCP.

R2 compiló y se cargó en COM6. Dos intentos devolvieron `+CME ERROR: operation
not allowed` con `CEREG=0,5`, PDP activo, `SMSTATE=0` y sin socket reportado.
La respuesta real a `AT+CAOPEN=?` enumeró cinco campos y no incluyó el campo
opcional `recv_mode`, aunque R2 lo enviaba como sexto parámetro. Por eso R1 y
R2 no son evidencia válida para atribuir la falla a radio, operador o proxy.
La bitácora está en `evidence/sim-tcpdiag-r2-20261029.log` y la captura del
build en `evidence/versions/2026-10-29_tcpdiag-r2-build/`.

## Perfil de diagnóstico TCP `1.1.0-tcpdiag-r3`

R3 envió `CAOPEN` con cinco parámetros y volvió a recibir `+CME ERROR: operation
not allowed` mientras registro y PDP seguían activos. `CASTATE: 0,0` y
`SMSTATE: 0` confirman que el socket estaba cerrado y MQTT desconectado. La
prueba sigue sin demostrar un intento de conexión hacia el proxy. El registro
está en `evidence/sim-tcpdiag-r3-20261029.log`.

## Perfil de diagnóstico TCP `1.1.0-tcpdiag-r4`

R4 selecciona explícitamente el identificador TCP con `AT+CACID=0` antes de
abrir el socket, y registra `CACID?`, `CAOPEN?` y `CACFG?` antes y después del
intento. Si el módem confirma `+CAOPEN: 0,0` y `CASTATE: 0,1`, entonces se
puede iniciar la observación prolongada de transporte. Si vuelve a rechazar
`CAOPEN`, se revisará la configuración interna del cliente TCP antes de
atribuirlo a la red o al broker.

## Datos mínimos del registro de cada versión probada

```text
Versión mostrada por el firmware:
Commit de Git:
SHA-256 del binario:
Perfil de PlatformIO y opciones de compilación:
Fecha, hora y zona horaria:
Placa, SIM7080G, SIM y antena:
Alimentación (USB / AC protegida / batería):
Duración y criterio de aceptación:
Resultado y eventos observados:
Respaldo SD y archivo de registro serie:
Recepción verificada en el backend (sí/no):
```

## Perfil SIM_AT por consola USB — 1.1.0-simat-usb-r1

Perfil `test_sim_at_usb`, con USB Serial/JTAG como consola primaria y OLED inicializada. En USB y sin AC, se capturaron nueve ciclos PASS seguidos de `ATI`, `CPIN`, `CSQ` e IMEI entre 14:02 y 14:04 del 29 de octubre. El snapshot y el manifiesto están en `evidence/versions/2026-10-29_simat-usb-r1-build/`; el resultado está en `docs/PRUEBA_SIM_AT_USB_R1_2026-10-29.md`.

## Perfil TCP R5 reconstruido — 1.1.0-tcpdiag-r5

Se recompiló y cargó en COM6 el 29 de octubre, con esptool verificando la escritura. Esta reconstrucción produjo SHA-256 `63DEB88069B02A3753FBBFFF3C1D73198D5DFC4E180420B333403B3669B6B325` y se conserva en `evidence/versions/2026-10-29_tcpdiag-r5-rebuild/`. El monitor no abrió COM6 por `PermissionError(13)`; no se obtuvo resultado TCP y la prueba se mantiene inconclusa. La OLED apagada es esperada en este perfil porque no la inicializa.

## TCP R6 probado por COM6 — 1.1.0-tcpdiag-r6

R6 evita volver a escribir `CACID=0` cuando el módem ya informa ese CID. Se
cargó y observó por COM6 el 29 de octubre, solo con USB y AC desconectada. El
ensayo alcanzó 16 intentos TCP, uno por minuto. Los 16 fueron rechazados en
318–319 ms con `+CME ERROR: operation not allowed`; durante las capturas el
registro estaba en roaming (`CEREG=0,5`), el PDP continuaba activo con IP,
LTE Cat-M1 permanecía en línea y el socket seguía cerrado (`CASTATE=0,0`).
No se confirmó ninguna apertura TCP y no se puede atribuir este rechazo al
broker. El binario SHA-256
`67103132B6C48D8539E6704F1AD5CCE8B177569632C1999283D926A1839B1D29`, fuentes
relevantes y configuración quedaron en
`evidence/versions/2026-10-29_tcpdiag-r6-usb-test/`; la bitácora completa está
en `evidence/sim-tcpdiag-r6-20261029.log`. Informe:
`docs/PRUEBA_TCP_R6_2026-10-29.md`.

## TCP R7: DNS separado de conexión — 1.1.0-tcpdiag-r7

R7 resuelve `shinkansen.proxy.rlwy.net` mediante `AT+CDNSGIP` y hace hasta
cinco intentos de `CAOPEN` a la IP resuelta. La DNS funcionó y devolvió
`66.33.22.231`; los cinco intentos TCP a esa IP y al puerto 58954 fueron
rechazados por el módem en unos 314 ms con `+CME ERROR: operation not allowed`.
El equipo siguió registrado (`CEREG=0,5`), con PDP activo e IP, y el socket
permaneció cerrado. Esto descarta un fallo de resolución DNS, pero no demuestra
que el paquete TCP haya llegado al broker. El firmware queda en espera después
de los cinco intentos para evitar reintentos indefinidos.

El binario SHA-256
`D6112F90BE4DDD006FD78C8B5782FA6AA0C2F84E09828D12A8DEC85B8EB631E6`, fuentes
relevantes y configuración están en
`evidence/versions/2026-10-29_tcpdiag-r7-dns-test/`; el registro serial está
en `evidence/sim-tcpdiag-r7-20261029.log`. Informe:
`docs/PRUEBA_TCP_R7_2026-10-29.md`.

## Reconexión ante cortes de datos — `1.1.0-mqtt-r2` y `1.1.0-obj4-q1-rc1`

Commit de código `f69c0b7` (rama `fix/firmware-reconexion-mqtt`, sobre el
checkpoint `3f5b8c9`). Motivo: el 29-oct se comprobó que los cortes de MQTT
son cortes de datos del lado celular (registro y PDP activos, sin paso de
datos; ~28 min arriba / 12–36 min abajo) y que el firmware los agravaba con
ciclos PDP cada ~68 s, barridos de baudios sobre un módem ocupado, reescritura
de CNMP/CMNB en cada re-probe y cierre de sesiones SMCONN completadas tarde.

| Perfil | Versión | SHA-256 del binario | Bytes |
|---|---|---|---|
| `test_sim_mqtt` | `1.1.0-mqtt-r2` | `B1EBD7130186BF26C54EE3EA4DBEAE02D40B0DF9538B219031CCA4A6B51167F8` | 502 464 |
| `integration_qos1_reconnect` | `1.1.0-obj4-q1-rc1` | `8FB149F84C4512F852935070E39FA4167BC27D1D31FED1A8FBE4C3C952599CB4` | 498 768 |

Estado: compilado, **pendiente de validación** con al menos dos cortes reales.
Criterios: ≤1 ciclo PDP por corte; ningún barrido de baudios ni PWRKEY con el
módem respondiendo en los 5 min previos; ninguna secuencia
CONNECT→DISCONNECT→CONNECT en el broker; primera publicación ≤60 s después de
que la red vuelve; respaldo SD drenado sin huecos ni duplicados.

Respaldo (31-oct): el binario `1.1.0-mqtt-r2` validado sin supervisión durante
40 h se copió con bootloader y particiones a
`evidence/versions/2026-10-31_mqtt-r2-respaldo-binario/`. El de
`1.1.0-obj4-q1-rc1` no se respaldó y se perdió cuando PlatformIO limpió
`.pio/build` al cambiar `platformio.ini`. Antes de promoverlo hay que
recompilarlo desde el commit y registrar su nuevo SHA-256, porque el binario
incluye la fecha de compilación. **Regla:** respaldar los binarios antes de
editar `platformio.ini`.

## Metrología en banco — `1.1.0-metro-r1`

Perfil `metrology_sd` (perfil integrado 0):

- **Activos:** ADE9153A, RTC, OLED, NVS y microSD.
- **Deshabilitados:** celular y MQTT (`METER_COMM_ENABLE=0`). No consume datos ni envía alertas por el aire.
- **Histórico SD:** cada 5 s (`METER_SD_HISTORY_PERIOD_MS=5000`), para reunir 30 pares por punto de P01 en 2,5 min.

Ajustes sin efecto en los demás perfiles:

- Guardas `#ifndef` para ambos parámetros.
- La tarea de comunicaciones solo se registra en el supervisor si está habilitada. Antes producía un falso `FAULT_TASK_STALL` a los 5 min.
- PWRKEY se fija en LOW en lugar de quedar flotante.

| Perfil | Versión | SHA-256 del binario | Bytes |
|---|---|---|---|
| `metrology_sd` | `1.1.0-metro-r1` | `9122C746EDDF73C2F5FF771123E43F927ACDA041E77EE653F8DB9AD977D05840` | 442 432 |

Respaldo en `evidence/versions/2026-10-31_metro-r1-build/`. Se graba con
`tools/metrology/grabar_respaldo.cmd`. Las herramientas de ensayo están en
`tools/metrology/` (ver su README).

Estado: compilado; pendiente de la prueba de humo en banco.

## Pruebas reales y verificación post-ajuste — `1.1.0-obj4-q1-rc2` y `1.1.0-metro-r2`

Compiladas el 2026-11-04 sobre `a483027` con cambios aún sin commit (el parche
está en `evidence/versions/2026-11-04_q1-rc2-build/`, junto a los binarios).

- **Ajuste de tensión:** `METER_ADE_AVGAIN_BENCH = -354737`. Se escribe en `AVGAIN` y se verifica por lectura en cada inicialización o recuperación del ADE. Corrige el +0,265 % medido contra el 34450A en el rango 100 V durante `NOCHE_12h_metro_20261103_run01` (18 ciclos). Afecta por igual a V, P, Q, S y energía. La telemetría lo informa con el bit `cal` = 16 (`CALIB_STATUS_BENCH`). Una calibración mSure persistida lo sustituye.
- **Colas SD:**
  - Una cabeza ilegible ya no detiene la entrega en silencio: línea vacía, JSON truncado (se retira y se emite `DATA_LOSS`) y línea de más de 2048 B (la cola pasa a `.badN` sin borrar nada).
  - Un error de lectura provoca un remontaje; tras tres seguidos, cuarentena.
  - El drenado ya no trunca la cola ante un error de lectura.
- **Alertas sin SD:** van a la cola RAM del despachador y se publican en vivo.
- **Fallo de ACK local:** ya no dispara una recuperación MQTT.
- **Eventos PQ:** inicio y fin de SAG, SWELL, FREQ y OVERTEMP quedan en `events_v1.jsonl` con severidad INFO. `/alerta` no cambia.
- **`/estado`:** se quitó el campo `online`, siempre `true`, para dejar margen frente al límite de 1024 B de SMPUB (antes 1014 B).

| Perfil | Versión | SHA-256 del binario | Bytes |
|---|---|---|---|
| `integration_qos1_rc2` | `1.1.0-obj4-q1-rc2` | `DED9701AC9AA7AD49B5950B6CCFEC5F7DA18EC9AAB740E39A0548495659DB73F` | 500 896 |
| `metrology_sd` | `1.1.0-metro-r2` | `F73FBDEEF4329B5729F78F3131D2E6C3E8254E4DAD387930B18A5898B5DF81C2` | 444 496 |
| `test_sd_service` | perfil 13 | `DE295CAD6A183888DF73B36F2DE36CEA225C7B4B70D30B068A11042F8DBA2E2F` | 504 768 |

Estado: `test_sd_service` en equipo aprobado (salvo `PRODUCERS`, falla preexistente del arnés); q1-rc2 grabado el 2026-11-04 21:03 UTC, AVGAIN verificado y `cal`=16 recibido en Railway. Detalle en `RESULTADOS.txt` de la carpeta de evidencia.

## Candidata para P05 — `1.1.0-obj4-q1-rc3` y `1.1.0-metro-r3`

Cambios respecto de q1-rc2:

- **Alertas persistidas:** salen en el ciclo de 1 s, antes que la telemetría. Antes salía una por latido, unos 15 s. En la corrida `INT_AC_carga_rc2` un atraso de unas 120 alertas retuvo más de una hora las de la prueba.
- **Perfiles sin comunicaciones** (`METER_COMM_ENABLE=0`): ya no encolan alertas en la SD. El evento sigue en `events_v1.jsonl`.
- **`test_sd_service`:** se corrigió el conteo de `PRODUCERS`, que daba un falso FAIL desde q1-rc1.

| Perfil | Versión | SHA-256 del binario | Bytes |
|---|---|---|---|
| `integration_qos1_rc3` | `1.1.0-obj4-q1-rc3` | `7B65590BDEA10B77393287501888195DE74CF19C1B7FFB1AE113C90CB8D3544B` | 500 928 |
| `metrology_sd` | `1.1.0-metro-r3` | `4839D95C23828A205B78D9829C28D9963964C6AC8A666E307F40DD223AE85782` | 443 584 |
| `test_sd_service` | perfil 13 | `3DD8F986AC6AE019B86CE4956C69659DC7F5DC3D539AF251D6D75EE3AA25EA1B` | 504 816 |

Estado:

- `test_sd_service` en equipo: 10/10 PASS.
- q1-rc3 grabado el 2026-11-04 a las 23:37 UTC (boot 3696470796).
- Atraso de alertas drenado a una alerta cada ~2,5 s; las alertas pendientes de la corrida rc2 llegaron a Railway.
- Pendiente: corrida de verificación `INT_AC_carga_rc3` (puerta de entrada a P05).

Evidencia: `evidence/versions/2026-11-04_q1-rc3-build/`.

## Escalera de recuperación celular — `1.1.0-obj4-q1-rc4`

Origen: E4-01 (2026-11-05). El módem contestó AT durante un corte de 4 h 40 min, con 93 registros fallidos. Solo se recuperó al reiniciar el módulo con el botón, y tardó menos de 1 min. q1-rc3 no escalaba: el PWRKEY solo actúa si el módem no contesta AT, y estaba inhibido en ese perfil.

Cambios respecto de q1-rc3:

- **Escalera** (`modules/communication/cell_recovery.c`, política pura con autoprueba de 33 pasos). El reloj cuenta desde la última publicación MQTT confirmada, no desde la última sesión: en E4-01 hubo sesiones que fallaban al primer SMPUB.
  - **N1:** `AT+CFUN=4/1` (solo la radio) a los 10 min; a los 3 min si CEREG informa 0 o 3. Como máximo uno cada 20 min. Con `CFUN=0` el banco dio `SIM failure` al volver.
  - **N2:** `AT+CFUN=1,1` a los 30 min. Como máximo uno por hora.
  - **N3:** `AT+CPOWD=1` y PWRKEY a los 90 min. Uno cada 2 h, con intervalo que se duplica hasta 8 h, y máximo 6 por día.
- **Sonda de datos** (`AT+SNPING4` a 8.8.8.8 y, si no responde, `AT+CDNSGIP`). Se usa cuando hay registro y PDP. Si responde, la falla es del broker: no se toca el módem salvo un N2 después de 45 min. En la primera conexión de banco hubo `SMCONN operation not allowed` con ping correcto, es decir, una pila MQTT del módem trabada.
- **Eventos:** cada escalón genera `SIM_MODEM/MODEM_RECOVERY`. N1 es INFO y solo queda en la SD. N2 y N3 son WARNING y llegan a /alerta; los cierra el `MODULE_READY` siguiente.
- **PWRKEY automático activo de nuevo.** Enciende el módem tras un arranque en frío (`ESP_RST_POWERON`). Tras un reinicio solo del ESP32 exige 5 min sin AT antes de pulsar.
- **Operador en modo automático** (`AT+COPS=0`). La preferencia `COPS=4,2,"732123"` se probó y **se descartó**: el módem la guarda como modo manual y, tras un CFUN o un reinicio, queda sin buscar red (banco run02). Como COPS se guarda en el módem, se verifica en cada inicialización completa; eso además repara un módem que haya quedado en manual. El detalle de `CELL_NO_SERVICE` incluye CEREG.
- **Sonda:** solo cuenta un eco de ping de menos de 3000 ms. Sin respuesta, el módem informa 60000 ms, y en run03 eso se tomaba como éxito.
- **SD:** segundo intento de lectura de la cabeza de la cola antes de remontar. En E4-01 hubo 5 remontajes durante el vaciado.
- **Ahorro de datos, solo perfil de producción:**
  - histórico SD y `/datos` cada 5 min; cada fila genera su registro de telemetría, así que la secuencia sigue consecutiva;
  - `/estado` cada 30 min;
  - resultado estimado: ~0,35 MB/día frente a ~1,5 MB/día con 60 s.

  El keepalive MQTT sigue en 120 s porque el broker impone `max_keepalive 120` y rechaza un CONNECT 3.1.1 que pida más. El backend marca el equipo como offline a los 5 min sin datos (`OFFLINE_UMBRAL_MIN`): conviene subir esa variable a 15 en Railway.

| Perfil | Versión | SHA-256 del binario | Bytes |
|---|---|---|---|
| `integration_qos1_rc4` | `1.1.0-obj4-q1-rc4` | ver `SHA256SUMS.txt` (recompilado el 2026-11-06 sin preferencia de operador, con N1 `CFUN=4` y sonda corregida) | — |
| `sin_datos_modem_apagado` | `1.1.0-obj4-q1-rc4-sindatos` | `6AD98A22E894519F5467CC6D794D799718B41E91D6C4279C23B285F8BA323477` | — |
| `integration_qos1_rc4_banco` | `1.1.0-obj4-q1-rc4-banco` | `DDFE224D75E1A1B6DC91EF3FEC1DF4D19BAE541D42C6662DCE653C264F83F45D` | 508 928 |

El binario `_banco` se compiló antes de dos cambios de configuración que no lo afectan: el valor por defecto de 45 min, que el perfil sobrescribe con 20 min, y el `#ifndef` de `/estado`. Ese binario se grabó el 2026-11-06 a las 03:18 UTC.

El perfil `_banco` tiene los tiempos acortados: N1 1 min, N2 3 min, N3 6 min. Además ejecuta la autoprueba de la política y un guion automático que empieza 3 min después de la primera publicación:

| Fase | Duración | Condición simulada | Resultado esperado |
|---|---|---|---|
| A | 12 min | Falla MQTT con registro y PDP reales | La sonda responde; **sin reinicios** del módem |
| B | 4 min | Normal | La cola se vacía |
| C | 16 min | Registro ausente | Se ejecutan N1, N2 y N3 reales |
| D | — | Normal | Recuperación y vaciado |

Perfil `sin_datos_modem_apagado`: sin celular ni MQTT, con el módem **apagado** al arrancar (`AT+CPOWD=1`, o PWRKEY si no obedece). ADE, RTC, OLED y SD siguen activos. Sirve para las noches entre ensayos sin gastar datos.

Resultados de banco en `evidence/versions/2026-11-05_q1-rc4-build/RESULTADOS_BANCO.md`:

- validados el encendido en caliente, la sonda, la fase A sin reinicios y la mecánica de N1, N2 y N3 con el módem real;
- descartada la preferencia de operador;
- corregidos N1 y el análisis del ping;
- **run04 (2026-11-06, `banco4` BA62EAEB…): fases C y D OK.** Escalera completa en 16 min. Al terminar la falla simulada, el módem se registró en 60 s y conectó MQTT en 66 s. Después, 23 min de publicación sin reintentos AT ni fallas. La autoprueba aparece "OK (33 pasos)" en los `events_v1.jsonl` de la SD.

El encendido en frío se verifica al inicio de E4-02, con el cargador de pared y sin pulsar el botón del módulo.

Estado anterior: compilados ambos; pendiente la prueba de banco por USB con la AC desconectada. Antes de E4-02 hay que verificar:

- encendido del módem en frío;
- registro con 732-123;
- que el ping responde en este APN;
- autoprueba OK;
- fases A a D.

Evidencia: `evidence/versions/2026-11-05_q1-rc4-build/`.
