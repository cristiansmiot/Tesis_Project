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
- MISO: GPIO13
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
