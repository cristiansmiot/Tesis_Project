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
