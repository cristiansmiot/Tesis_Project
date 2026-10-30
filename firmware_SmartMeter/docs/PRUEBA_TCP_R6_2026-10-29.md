# Prueba TCP R6 del SIM7080G — 29 de octubre de 2026

## Propósito

Observar intentos repetidos de apertura TCP desde el SIM7080G hacia el proxy
público de Mosquitto, sin publicar telemetría ni ejecutar MQTT, ADE o microSD.
El objetivo es separar un rechazo inmediato del módem de un timeout de red o
de una desconexión de una sesión ya abierta.

## Montaje y condiciones

- Perfil: `test_sim_tcp`, firmware `1.1.0-tcpdiag-r6`.
- Placa alimentada solo por USB; AC desconectada.
- COM6 conectado al monitor serie.
- El usuario informó que la OLED estaba encendida.
- Se observó el SIM7080G con PWR encendido y NET parpadeando.
- Destino: `shinkansen.proxy.rlwy.net:58954`.
- Intervalo configurado entre intentos: 60 s.
- La prueba excluye ADE, microSD, MQTT CONNECT y telemetría.

## Resultado observado

Entre las 14:56:31 y las 15:11:32, hora local, se capturaron 16 intentos de
`AT+CAOPEN=0,0,"TCP","shinkansen.proxy.rlwy.net",58954`, separados por
aproximadamente 60 s. Los 16 terminaron en 318–319 ms con:

```text
+CME ERROR: operation not allowed
```

En cada captura posterior al rechazo se mantuvieron:

- `CEREG: 0,5`: registrado en roaming.
- `CNACT: 0,1,"10.66.42.214"`: contexto PDP activo con dirección IP.
- `CPSI`: LTE Cat-M1 en línea; RSRP observado aproximadamente entre −77 y
  −85 dBm, sin pérdida de registro durante la ventana.
- `CASTATE: 0,0`: socket cerrado.
- `CACID: 0` y `SMSTATE: 0`.
- `AT+CAOPEN=?`: capacidades que incluyen CID 0, PDP 0, TCP y puertos 1–65535.

No hubo una respuesta `+CAOPEN: 0,<resultado>` ni un socket abierto. Por eso,
esta prueba **no demuestra que el broker haya recibido una conexión TCP**: el
módem rechazó el comando antes de confirmar la apertura. Tampoco es evidencia
de un timeout del broker ni de una pérdida de radio. La sesión MQTT anterior
con ciclos horarios y este ensayo son fenómenos distintos; este último perfil
no llegó a establecer ni su primera conexión TCP.

La sintaxis de `CAOPEN` y sus parámetros coincide con el formato documentado
por SIMCom. La respuesta `operation not allowed` se debe tratar como un error
del módem o de sus condiciones internas, no como uno de los resultados de
socket de `CAOPEN` (por ejemplo, rechazo remoto, error de resolución o red
inactiva). El manual SIMCom vigente publicado para SIM7080G es el
`SIM7070_SIM7080_SIM7090 Series_AT Command Manual_V1.07`.

## Interpretación y siguiente diagnóstico

La prueba confirma un rechazo inmediato y repetible en el comando de apertura
TCP. El registro celular, el PDP, la calidad de señal, el puerto y los rangos
de los parámetros consultados no muestran una condición obviamente inválida.
Con esta evidencia no es posible decidir si el origen restante es una
condición interna del módem, la interacción entre el perfil de conexión y el
PDP, o una restricción de red.

La siguiente versión diagnóstica debe consultar y registrar `AT+CEER` justo
después de cada error, además de `AT+CGACT?`, `AT+CGDCONT?`, `AT+CGPADDR=0` y
`AT+CDNSGIP="shinkansen.proxy.rlwy.net"`. Esto permitirá revisar causa
extendida, estado del contexto PDP, dirección asignada y resolución DNS. Debe
conservarse el ensayo USB con AC desconectada. No se recomienda volver a probar
la conexión MQTT de producción hasta obtener un `+CAOPEN: 0,0` y confirmar
`CASTATE: 0,1`.

## Evidencia y reproducibilidad

- Registro serie completo: `evidence/sim-tcpdiag-r6-20261029.log`.
- Código, configuración y binario probado:
  `evidence/versions/2026-10-29_tcpdiag-r6-usb-test/`.
- SHA-256 del binario: `67103132B6C48D8539E6704F1AD5CCE8B177569632C1999283D926A1839B1D29`.
- Tamaño del binario: 494 480 bytes.
- Revisión indicada por el firmware: `842a3fd-dirty`; los cambios locales no
  estaban comprometidos en Git.
- El cierre del monitor serie terminó la captura; no se reinició el broker ni
  se modificó Railway o la base de datos.

