# Perfiles de prueba de hardware sin ADE9153A

## Nuevos perfiles de servicio SD/OLED

### Servicio microSD (`test_sd_service`)

```powershell
pio run -e test_sd_service -t upload --upload-port COM6
pio device monitor -p COM6 -b 115200
```

Usa exclusivamente `/codex_sd_test` en la tarjeta. Verifica montaje,
recuperacion `.tmp`, registros con hora invalida y valida inyectada, productores
concurrentes, saturacion no bloqueante, umbrales 80/90% simulados, purga
recuperable y contadores `DATA_LOSS`, y una corrida de 30 minutos. No formatea
ni borra archivos fuera del directorio de prueba.

### OLED + RTC + servicio microSD (`test_oled_rtc_sd_service`)

```powershell
pio run -e test_oled_rtc_sd_service -t upload --upload-port COM6
pio device monitor -p COM6 -b 115200
```

Ejecuta las tareas reales de tiempo, almacenamiento, UI y comunicaciones con
ADE/metrologia deshabilitados. Mantener vacio el portabateria del RTC. `TIME`
puede quedar degradado por OSF o falta de respaldo sin impedir las escrituras
SD. No retirar ni insertar la microSD con la PCB energizada.

Esta guia permite validar por separado la OLED, el RTC DS3231, la microSD y
el SIM7080G sobre la PCB antes de ejecutar una prueba conjunta. Ninguno de los
perfiles individuales inicializa el ADE9153A ni requiere energizar la entrada
de red AC.

## Seguridad y revision antes de alimentar

1. No conectar LINEA, NEUTRO ni CARGA durante estas pruebas de baja tension.
2. Con la PCB apagada, verificar continuidad de GND y ausencia de cortos entre
   GND, 3V3, 5V y VBAT.
3. Alimentar primero con una fuente limitada en corriente, sin SD, sin SIM y
   sin OLED. Confirmar 5 V y 3.3 V antes de insertar los modulos.
4. Comprobar visualmente polaridad de reguladores, capacitores, diodos, RTC y
   conector de la microSD.
5. Confirmar que TX y RX del SIM estan cruzados: GPIO16 TX del ESP32 llega a RX
   del modem, y GPIO17 RX del ESP32 llega a TX del modem.
6. Mantener una tierra comun entre la alimentacion del SIM7080G y el ESP32.
   No alimentar el modem desde el pin 3V3 de la DevKit.

## Preparacion de la microSD de 8 GB

- Crear una sola particion primaria FAT32 con tabla MBR.
- Se recomienda tamano de unidad de asignacion de 32 KB.
- El firmware no formatea automaticamente una tarjeta que no pueda montar.
- Insertar o retirar la tarjeta solamente con la PCB apagada durante las
  primeras pruebas.

El perfil SD crea o extiende `/hwtest_sd.txt`. Cada arranque escribe 16
registros con un identificador de sesion y luego relee el archivo para comprobar
los 16 registros. El archivo se conserva como evidencia y puede revisarse en un
computador.

## Comandos comunes

Ejecutar los comandos desde `firmware_SmartMeter`:

```powershell
pio run -e NOMBRE_PERFIL -t upload
pio device monitor -b 115200
```

Si `pio` no esta en el PATH:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e NOMBRE_PERFIL -t upload
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" device monitor -b 115200
```

En el monitor, la linea que decide el resultado tiene este formato:

```text
HWTEST RESULT <MODULO> PASS - detalle
HWTEST RESULT <MODULO> FAIL - detalle
```

Los perfiles reintentan automaticamente cuando pierden un dispositivo. Esto
permite observar fallas intermitentes de alimentacion, soldadura o bus.

## Orden de ejecucion

### 1 OLED

Perfil: `test_oled`

```powershell
pio run -e test_oled -t upload
```

Conexiones esperadas:

- VCC: 3.3 V
- GND: GND
- SDA: GPIO8
- SCL: GPIO9
- Direccion: `0x3C`

La pantalla debe mostrar texto en ocho filas y un contador que aumenta cada
segundo. Dejarla funcionando al menos 15 minutos. La imagen debe permanecer
completa, sin parpadeos ni reinicios.

### 2 RTC DS3231

Perfil: `test_rtc`

```powershell
pio run -e test_rtc -t upload
```

Conexiones esperadas:

- VCC: 3.3 V
- GND: GND
- SDA: GPIO8
- SCL: GPIO9
- Direccion: `0x68`

El perfil comprueba que el reloj avance y muestra hora y temperatura cada
segundo. Un `PASS` valida comunicacion y oscilador, pero la hora mostrada tambien
debe compararse manualmente con UTC.

Despues del primer `PASS`, apagar la PCB entre 5 y 10 minutos conservando la
bateria del RTC. Al encender nuevamente, la hora debe haber avanzado. Revisar el
tipo de bateria y el circuito del modulo antes de usar una CR2032 no recargable.

### 3 MicroSD

Perfil: `test_sd`

```powershell
pio run -e test_sd -t upload
```

Conexiones esperadas:

- MOSI: GPIO11
- MISO: GPIO13, mediante buffer triestado habilitado por CS
- SCLK: GPIO12
- CS: GPIO7
- Alimentacion y niveles logicos: 3.3 V

Criterio de aceptacion:

```text
HWTEST RESULT SD PASS - FAT montado; 16/16 registros verificados; ...
```

Ademas, la capacidad total reportada debe ser razonable para una tarjeta
comercial de 8 GB. No sera exactamente 8192 MB por la diferencia entre unidades
decimales y binarias y por el espacio del sistema de archivos.

### 4 SIM7080G UART y comandos AT

Perfil: `test_sim_at`

```powershell
pio run -e test_sim_at -t upload
```

Este perfil no exige registro celular ni MQTT. Comprueba:

- deteccion automatica de baud rate y operacion posterior a 57600;
- respuesta a `ATI`;
- SIM lista mediante `AT+CPIN?`;
- respuesta de senal mediante `AT+CSQ`;
- lectura de IMEI.

Si no responde, revisar primero fuente del modem, PWRKEY, antena, GND comun y
cruce TX/RX.

### 4.1 SIM7080G PWRKEY

Perfil: `test_sim_pwrkey`

```powershell
pio run -e test_sim_pwrkey -t upload
```

Con el modem inicialmente encendido, este perfil confirma AT, lo apaga de forma
mediante un pulso alto de 3 s sobre GPIO4, comprueba que UART dejo de responder
y aplica un segundo pulso para volverlo a encender. Finalmente valida AT y SIM.
En el HAT Waveshare, el pin externo `PWR` controla mediante una resistencia de
4.7 kOhm la base de un transistor NPN que invierte la senal hacia el PWRKEY de
1.8 V del modem. Por eso GPIO4 se mantiene normalmente en LOW y se pulsa en HIGH.

### 5 SIM7080G registro y datos

Perfil: `test_sim_network`

El perfil valida registro de red, activacion PDP y permanencia del contexto de
datos. Cada 15 s revisa estado de red y señal. Mantener la entrada AC
desconectada; no inicializa ADE ni microSD.

### 5.1 Observacion prolongada de red sin MQTT

Perfil: `test_sim_network_long`; version: `1.1.0-netdiag-r1`.

```powershell
pio run -e test_sim_network_long -t upload --upload-port COM6
pio device monitor -e test_sim_network_long --port COM6 --baud 115200
```

Ejecutar durante al menos 70 min desde el primer mensaje `NETDIAG READY`.
Mantener la misma SIM, antena, ubicacion y alimentacion USB de la prueba MQTT;
AC desconectada y ADE/microSD fuera de uso. No reiniciar ni mover la antena.

El perfil no usa MQTT. Cada 15 s comprueba registro, adjuncion y contexto PDP,
y consulta `CSQ`; cada 5 min registra `CEREG`, `COPS`, `CNACT` y `CPSI`. Al
detectar una perdida, guarda el error, el tiempo monotónico y consultas AT
inmediatas antes de volver a intentar el registro. Esto permite observar si el
ciclo aparece en la capa celular/PDP sin una sesion MQTT activa.

`CEREG=5` significa registrado en roaming; por si solo no identifica al
operador como causa de la falla. `COPS` permite comparar PLMN/tecnologia y
`CPSI` la celda y la radio a lo largo del ensayo. Este perfil no prueba acceso
TCP a Railway ni entrega de MQTT: si el registro y PDP permanecen sanos, se
necesitara una prueba de transporte para aislar la ruta de datos.

Resultado de la corrida del 29 de octubre: inicio válido 11:29:34, fin
12:40:07, 70 min 33 s, 281 muestras CSQ, 15 grupos de estados celulares y
cero caídas espontáneas de registro/PDP. El primer intervalo se excluyó porque
el módem se apagó al pulsar manualmente PWRKEY. El análisis está en
`docs/PRUEBA_RED_CELULAR_SIN_MQTT_2026-10-29.md`.

### 5.2 Prueba periódica de transporte TCP

Perfil: `test_sim_tcp`; versión actual `1.1.0-tcpdiag-r4`.

```powershell
pio run -e test_sim_tcp -t upload --upload-port COM6
pio device monitor -e test_sim_tcp --port COM6 --baud 115200
```

Con USB y AC desconectada, el perfil abre un socket TCP al proxy público del
broker (`shinkansen.proxy.rlwy.net:58954`) cada 60 s y lo cierra después de
confirmar `CASTATE`. No envía `MQTT CONNECT`, publicaciones ni datos de usuario.
Registra las respuestas de `CAOPEN`, `CASTATE` y `CACLOSE`; también consulta
las capacidades `CAOPEN=?`, estado del cliente MQTT `SMSTATE?` y configuración
`CACFG?`. Ante un fallo, consulta registro celular, operador, PDP y radio.
Mantener el mismo SIM, antena, ubicación y fuente que en la prueba de red; no
pulsar PWRKEY ni RESET.

R2 mostró que el comando `AT+CAOPEN=?` de este módem anuncia cinco campos y no
incluye el parámetro opcional `recv_mode` del manual general. R3 omitió ese
parámetro, pero igualmente recibió `+CME ERROR: operation not allowed` con
registro y PDP activos. R4 selecciona primero `CACID=0` y consulta `CACID?`,
`CAOPEN?` y `CACFG?`. Primero confirmar que el módem devuelve `+CAOPEN: 0,0` y
que `CASTATE` cambia a conectado; solo entonces continuar la observación por al
menos 70 min desde `TCPDIAG READY`. Si vuelve `+CME ERROR`, guardar la respuesta
completa y no atribuirla a la cobertura ni al broker. Este perfil no mide la
entrega de telemetría ni sustituye una corrida MQTT.

### 5.3 MQTT aislado sobre SIM7080G

Perfil: `test_sim_mqtt`

```powershell
pio run -e test_sim_mqtt -t upload --upload-port COM6
pio device monitor -e test_sim_mqtt --port COM6
```

Usa el cliente MQTT del firmware sobre la red celular, sin ADE, microSD ni
publicacion de variables eléctricas. Comprueba la conexion, consulta `SMSTATE`
cada 15 s y publica cada minuto un JSON de diagnóstico QoS 1 en
`medidor/<id>/diagnostico`, con `retain=0`. El backend reconoce el formato del
topic como un tipo no procesado: no crea mediciones ni eventos en la base de
datos.

El perfil imprime la version exacta al iniciar. Ante `SMCONN` con
`operation not allowed`, intenta una sola recuperacion del contexto PDP y
comprueba que el modem confirme el contexto desactivado antes de volver a
activarlo. Limita estos ciclos a uno por minuto. La siguiente conexion MQTT se
intenta en el siguiente ciclo del perfil; no repite `SMCONN` de inmediato.

`MQTT_ISO PUBLISH PASS` significa que el SIM7080G completó `SMPUB` y que el
firmware confirmó `SMSTATE=1` después de publicar. No equivale a conciliar una
recepción independiente en la base de datos. Los fallos registran el código,
`CEREG`, `CNACT`, `SMSTATE` y la etapa/tiempo de recuperación. Mantener USB,
AC desconectada y no reiniciar durante la corrida.

### 6 OLED y RTC sobre el bus I2C compartido

Perfil: `test_oled_rtc`

Este es el primer perfil de integracion. Inicializa unicamente la OLED SSD1306
en `0x3C` y el RTC DS3231 en `0x68`, ambos sobre SDA GPIO8 y SCL GPIO9. Verifica
que el reloj avance y actualiza cada segundo en la OLED la fecha, hora,
temperatura y un contador `PASS`. La SD, el SIM7080G y el ADE9153A no se
inicializan.

```powershell
pio run -e test_oled_rtc -t upload --upload-port COM6
pio device monitor -p COM6 -b 115200
```

### 7 OLED, RTC y microSD

Perfil: `test_oled_rtc_sd`

Mantiene OLED y RTC activos sobre I2C mientras monta la microSD por SPI en
MOSI GPIO11, MISO GPIO13, SCLK GPIO12 y CS GPIO7. Al inicio verifica 16/16
registros y despues agrega una evidencia a `hwtest_combo.txt` cada 15 ciclos.
La OLED muestra la hora, temperatura, espacio libre, contador y cantidad de
escrituras. SIM7080G y ADE9153A permanecen deshabilitados.

```powershell
pio run -e test_oled_rtc_sd -t upload --upload-port COM6
pio device monitor -p COM6 -b 115200
```

```powershell
pio run -e test_sim_network -t upload
```

Despues de las pruebas AT, espera registro celular, activa el PDP con el APN
configurado y consulta `CEREG`, `COPS`, `CNACT` y `CSQ`. No conecta MQTT, de modo
que una falla de broker no puede ocultar el estado real de la red celular.

La primera espera de red puede tardar hasta 120 segundos. Un resultado aprobado
debe indicar registro y PDP activos y debe repetirse cada 15 segundos sin perder
el modem.

### 6 Integracion completa sin ADE9153A

Perfil: `test_all_no_ade`

```powershell
pio run -e test_all_no_ade -t upload
```

Este perfil ejecuta el firmware integrado con:

- OLED y botones;
- RTC DS3231;
- microSD;
- SIM7080G, red y MQTT;
- supervisor de tareas.

No inicializa ADE, mediciones, calidad de energia ni calibracion. Los valores de
metrologia permanecen en cero durante esta etapa.

Secuencia recomendada:

1. Ejecutar 30 minutos con OLED y RTC.
2. Repetir 30 minutos agregando la microSD.
3. Agregar el SIM7080G y confirmar red y MQTT.
4. Desconectar la red celular o apagar el modem y verificar que el ESP32, OLED,
   RTC y SD continuen operativos.
5. Ejecutar dos horas sin reinicios ni perdidas de bus.
6. Finalmente ejecutar la prueba continua de 24 horas.

## Regreso al firmware normal

El entorno predeterminado conserva el comportamiento original:

```powershell
pio run -e esp32s3_devkitc_1 -t upload
```

Ese entorno vuelve a habilitar el arranque normal, incluido el ADE9153A.

## Prueba digital aislada del ADE9153A

Perfil: `test_ade_smoke`

Esta es la primera prueba despues de conectar el EV-ADE9153ASHIELDZ. No usa el
SIM, MQTT, OLED, RTC, microSD ni entradas de red electrica. Inicializa SPI2,
resetea y configura el ADE9153A, comprueba RUN e identidad, exige 30 lecturas
consecutivas coherentes y valida la temperatura interna. Despues mantiene un
sondeo cada cinco segundos.

Condiciones previas:

1. Mantener desconectados LINE, NEUTRAL y cualquier conductor de carga.
2. Con la alimentacion apagada, confirmar GND comun y alimentacion de 5 V del
   shield. La guia UG-1253 indica que el EV-ADE9153ASHIELDZ se alimenta a 5 V.
3. Para el ESP32-S3, dejar abierto el jumper `5 V SPI` y cerrado el jumper
   `3 V SPI`. No alimentar las entradas GPIO del ESP32 con logica de 5 V.
4. Cableado configurado: MOSI GPIO11, MISO GPIO13, SCLK GPIO12, CS GPIO10,
   RESET GPIO14 e IRQ GPIO47.
5. Si la microSD permanece conectada, su rama MISO debe quedar aislada por un
   buffer triestado cuando SD_CS GPIO7 este alto.

Compilar y cargar, sustituyendo `COMx` por el puerto verificado:

```powershell
pio run -e test_ade_smoke -t upload --upload-port COMx
pio device monitor -p COMx -b 115200
```

El criterio de aceptacion es que aparezcan todos estos resultados:

```text
HWTEST RESULT ADE SPI PASS
HWTEST RESULT ADE ID PASS
HWTEST RESULT ADE SPI STABILITY PASS - 30/30 lecturas coherentes
HWTEST RESULT ADE TEMPERATURE PASS
HWTEST RESULT ADE_SMOKE PASS
```

Un patron todo `0x0000` o todo `0xFFFF` es FAIL y apunta primero a
alimentacion, GND, jumper de nivel logico, CS, MISO, SCLK o RESET. Esta prueba
solo demuestra comunicacion digital, identidad, DSP y sensor interno; no
demuestra exactitud, calibracion mSure ni medicion de tension/corriente.

Ejecucion del 22 de octubre de 2026: PASS en placa por COM6. Se obtuvieron
30/30 lecturas coherentes, `RUN=0x0001`, `VERSION=0x0025`,
`PRODUCT=0x0009153A`, temperatura interna de 24.78 C y monitor continuo con
`ready=1`, `bus_off=0` y `ESP_OK`.

La integracion del mismo dia detecto que el modulo microSD probado no liberaba
MISO de forma compatible al compartir GPIO13. Con la SD completa conectada, el
ADE devolvia identidad en cero; al retirar solo `MISO/DO` de la SD, recupero
inmediatamente `RUN=0x0001`, `VERSION=0x0025` y lecturas estables. La correccion
para la PCB ensamblada es aislar `MISO_SD` con un buffer triestado controlado
por `SD_CS`, conservando el bus SPI2 y el mapa de pines.

## Prueba de configuracion y adquisicion del ADE9153A

Perfil: `test_ade_measurement`

Este perfil es el siguiente paso despues de `test_ade_smoke`. Funciona con la
microSD fisicamente desconectada y no inicializa SIM, MQTT, OLED ni RTC. Primero
lee y compara 14 registros criticos: `RUN`, ganancia de los canales, seleccion
de 60 Hz, configuracion DSP, acumulacion de energia, temperatura, `VLEVEL` y el
divisor de tension. Luego toma 30 grupos de lecturas crudas y convertidas.

Fase inicial segura, sin red electrica:

1. Desconectar completamente la microSD.
2. Mantener LINE, NEUTRAL y CARGA desconectados.
3. Mantener el jumper SPI del shield en 3.3 V.
4. Conectar el ESP32 por USB y localizar su puerto con `pio device list`.
5. Cargar y abrir el monitor:

```powershell
pio run -e test_ade_measurement -t upload --upload-port COMx
pio device monitor -p COMx -b 115200
```

El criterio de aceptacion digital es:

```text
HWTEST RESULT ADE MEAS ID PASS
HWTEST RESULT ADE CONFIG READBACK PASS - 14/14 registros coinciden
HWTEST RESULT ADE CONVERSION MODULES PASS
HWTEST RESULT ADE MEASUREMENT READS PASS - 30/30 grupos de registros leidos
HWTEST RESULT ADE_MEASUREMENT PASS
```

Cada muestra genera dos lineas copiables a CSV: `ADE_RAW` contiene AVRMS,
AIRMS, potencias, APF, periodo y energia en registros; `ADE_SI` contiene la
bandera `AC_VALID`, V, A, W, VAR, VA, factor de potencia, Hz, Wh y temperatura.
Sin LINE/NEUTRAL, `AC_VALID=0` y no deben interpretarse PF ni frecuencia aunque
sus registros muestren valores por defecto. Las magnitudes restantes pueden ser
cero o ruido bajo. Esta fase aprueba configuracion y adquisicion, no exactitud.

Ejecucion del 22 de octubre de 2026: PASS en COM6, con microSD y entradas AC
desconectadas. Se verificaron 14/14 registros y 30/30 grupos de adquisicion. El
ruido estable observado fue aproximadamente 0.0109 V y 0.000526 A equivalentes;
la temperatura interna estuvo aproximadamente entre 25.1 C y 26.2 C. Estos
valores son linea base de diagnostico, no una calibracion metrologica.

La siguiente fase reutiliza el mismo perfil con una conexion AC protegida y
supervisada: primero tension sin carga, despues una carga resistiva conocida y,
finalmente, comparacion contra el instrumento patron. No energizar el shield
expuesto ni modificar conexiones con el circuito alimentado.

## Prueba integral de funciones del ADE9153A

Perfil: `test_ade_full`

Ejecuta una prueba no destructiva de las rutas implementadas: identidad,
acceso SPI 8/16/32 bits, escrituras idempotentes, readback de configuracion,
27 registros, conversiones V/I/P/Q/S/PF/energia/temperatura, banderas de
calidad de energia, interfaz mSure y las recuperaciones PROBE, SOFT_INIT y
SPI_REINIT. La microSD debe permanecer desconectada.

```powershell
pio run -e test_ade_full -t upload --upload-port COM6
pio device monitor -p COM6 -b 115200
```

El resultado global esperado es `HWTEST RESULT ADE_FULL PASS`. Vpeak y
armonicos quedan identificados como no implementados; el canal B permanece
deshabilitado por configuracion. La autocalibracion mSure real no se ejecuta
en este perfil porque modifica ganancias. El protocolo y la matriz completa
estan en `docs/ADE9153A_FULL_TEST_PROTOCOL_2026-10-23.md`.

## Integracion funcional temporal sin microSD

Perfil: `integration_no_sd`

Arranca ADE, medicion, calidad de energia, RTC, OLED, SIM7080G y MQTT, pero no
inicia almacenamiento. La SD se reporta `DISABLED` y `/datos` se publica en
vivo sin auditoria local ni reenvio despues de una caida de red.

```powershell
pio run -e integration_no_sd -t upload --upload-port COM6
pio device monitor -p COM6 -b 115200
```

### 10 Contratos comunes del sistema

Perfil: `test_system_contracts`

Es una autoprueba aislada que no requiere cambiar conexiones ni disponer de
periféricos. Verifica estados y transiciones, deduplicación de fallas, tiempo
válido e inválido, acceso concurrente, saturación de la cola y reemplazo por
prioridad.

```powershell
pio run -e test_system_contracts -t upload --upload-port COM6
pio device monitor -p COM6 -b 115200
```

El resultado esperado es `SYSTEM CONTRACTS: RESULT PASS`. Después se debe
volver a cargar `test_all_no_ade` o el perfil del hardware que se vaya a probar.

### 11 Servicio RTC aislado

Perfil: `test_rtc_service`

Ejecuta la tarea RTOS real de tiempo, sin OLED, SD, SIM ni ADE. Comprueba que
el DS3231 arranque como fuente UTC, que una fecha inválida no se use y que el
módulo pase por `RECOVERING`, `DEGRADED` o `FAILED` sin reiniciar el ESP32.

```powershell
pio run -e test_rtc_service -t upload --upload-port COM6
pio device monitor -p COM6 -b 115200
```

El resultado estable esperado es una línea por segundo con `valid=1`,
`source=RTC`, epoch creciente y `state=OK`.

### 12 Servicio RTC y OLED

Perfil: `test_oled_rtc_service`

Usa el mismo servicio RTOS que el firmware integrado y comparte el bus I2C
mediante un mutex estático. La OLED muestra fuente, hora local de Bogotá,
temperatura, indicador OSF y contador. SD, SIM y ADE permanecen deshabilitados.

```powershell
pio run -e test_oled_rtc_service -t upload --upload-port COM6
pio device monitor -p COM6 -b 115200
```

Si el RTC nunca se ha puesto en hora, se puede compilar una sola vez con
`METER_RTC_TEST_SET_EPOCH` definido a un epoch UTC válido. El perfil entrega
esa hora al servicio como una fuente remota, verifica la escritura del RTC y
borra OSF solamente después de releer correctamente el DS3231. No se debe dejar
un epoch fijo en el firmware de producción.

Criterios de aceptación:

1. OLED: `RTC SERVICE PASS`, `VALID:SI`, `SRC:RTC` o `SRC:REMOTE` y contador
   creciente sin parpadeos.
2. Serial: `HWTEST RESULT RTC_SERVICE PASS` y epoch creciente.
3. Desconectar solo el DS3231 provoca falla del RTC, pero OLED y ESP32 siguen
   funcionando; al reconectarlo se genera recuperación sin reinicio global.
4. Apagar toda la PCB de 5 a 10 minutos dejando la batería instalada. Al
   energizar, la hora debe haber avanzado y la fuente debe volver a `RTC`.
5. Retirar la batería y la alimentación puede activar OSF. En ese caso la hora
   se considera inválida hasta una sincronización celular/remota válida.
