# Ensayo TCP R3 del SIM7080G — 29 de octubre de 2026

## Propósito

Repetir la prueba de apertura TCP sin el parámetro opcional `recv_mode`, y verificar si el módem alcanza el proxy público del broker.

## Resultado

Se cargó `1.1.0-tcpdiag-r3` en COM6. El primer intento capturado, a las 13:16:26 (hora del computador), envió `AT+CAOPEN=0,0,"TCP","shinkansen.proxy.rlwy.net",58954` y recibió `+CME ERROR: operation not allowed` en 313 ms. Las consultas posteriores reportaron:

- `CEREG: 0,5` (registro en roaming).
- `CNACT: 0,1` con IP `10.66.42.214` (PDP activo).
- `CPSI`: LTE CAT-M1 en línea.
- `CASTATE: 0,0` (socket identificado cerrado).
- `SMSTATE: 0` (cliente MQTT desconectado).

## Interpretación

El resultado R3 demuestra que quitar el sexto parámetro no basta para abrir el socket. El módem devuelve un error de operación antes de emitir un resultado TCP como DNS fallido, conexión rechazada o timeout. Por eso todavía no se puede señalar al operador, a Railway ni a la cobertura como causa.

La siguiente verificación seleccionará explícitamente el identificador del socket con `AT+CACID=0` antes de `CAOPEN`, y registrará `CACID?`, `CAOPEN?` y `CACFG?`. Si persiste el rechazo, se detendrá la prueba TCP y se revisará el estado/configuración del cliente de sockets del módem antes de repetir.

## Evidencia

Log serial: `evidence/sim-tcpdiag-r3-20261029.log`. Fuentes exactas y binario: `evidence/versions/2026-10-29_tcpdiag-r3-build/`.
