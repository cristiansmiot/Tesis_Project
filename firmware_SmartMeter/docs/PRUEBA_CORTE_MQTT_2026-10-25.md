# Prueba de indisponibilidad y recuperacion MQTT

Fecha: 25 de octubre de 2026.
Dispositivo: `SM-348518435344`.
Firmware: `1.1.0-obj4`.

## Objetivo

Comprobar que el medidor mantiene ADE, RTC y microSD operativos durante una
indisponibilidad deliberada del broker, que reconecta automaticamente y que
las mediciones pendientes llegan posteriormente al backend sin perdida neta.

Esta prueba interrumpio exclusivamente el despliegue Mosquitto. El medidor,
la AC, el ESP32, el ADE9153A, el RTC, la microSD y el SIM7080G permanecieron
energizados. No se retiro la antena y no se reinicio el medidor.

## Linea de tiempo

| Hora Bogota | Observacion |
| --- | --- |
| Antes de 10:30 | OLED: `ADE:OK`, `RTC:OK`, `SD:OK`, `CEL:OK`, `MQTT:OK`; se observaron mas de dos lecturas. |
| 10:26:23 | Ultima linea base consultada antes del corte: `boot_id=2716100442`, `seq=4`, hora valida. |
| 10:30 | Se retiro el despliegue activo del broker en Railway US West. OLED paso por `MQTT:REC`, `MQTT:FAIL` y nuevos intentos `REC`. |
| 10:42-10:47 aprox. | Los intentos de redeploy/rollback fallaron antes de iniciar Mosquitto. Railway informo imposibilidad de descargar la imagen desde su registro de US West. No hubo logs de aplicacion. |
| 10:58:56 | La plataforma registro el evento `mqtt_disconnected` como `cleared`. |
| 10:59 aprox. | Broker operativo tras migrar el mismo servicio a US East; se conservaron `shinkansen.proxy.rlwy.net:58954 -> 1883` y `MQTT:OK` regreso sin reflashear. |
| 11:15:55 | Fin de la observacion posterior: `seq=53` recibida; persistian cuatro huecos. |

La indisponibilidad observada fue de aproximadamente 29 minutos, mayor que
los 10 minutos planeados debido al incidente de despliegue en Railway. Se
conserva como una prueba valida de indisponibilidad prolongada del broker.

## Resultado de plataforma

Ventana consultada: `2026-10-25T15:25:00Z` a
`2026-10-25T16:16:00Z` (10:25 a 11:16 Bogota).

| Indicador | Resultado |
| --- | ---: |
| `boot_id` | `2716100442` |
| Reinicios durante la ventana | 0 |
| Mediciones generadas reportadas | 51 |
| Intentos de publicacion | 51 |
| Publicaciones aceptadas por el modem | 51 |
| Mediciones unicas recibidas por backend | 47 |
| Rango de telemetria observado | `seq=3..53` |
| Secuencias ausentes despues de 15 min | `8, 9, 10, 11` |
| PDR eventual observado al cierre | 92.16 % |
| Duplicados / conflictos de identidad | 0 / 0 |
| Latencia p50 / p95 / maxima | 142.495 / 1360.395 / 1495.783 s |
| Reconexiones MQTT nuevas | 1 |
| Alarmas en la ventana | 1 `mqtt_disconnected`, resuelta |
| ADE disponible en estados recibidos | 100 % |
| AC presente en estados recibidos | 100 % |
| RSRP observado | -125 a -121 dBm |

La elevada latencia es esperada para los mensajes recuperados despues del
corte. La senal era debil, pero la causa impuesta de esta prueba fue la
ausencia del broker: el servicio Mosquitto estaba retirado y no existia un
contenedor capaz de aceptar conexiones.

## Dictamen

La prueba **aprueba parcialmente**:

- El dispositivo no se reinicio y ADE, RTC, SD y generacion de datos
  continuaron operativos.
- La deteccion, alarma, reconexion automatica y limpieza de la alarma MQTT
  funcionaron.
- El endpoint TCP se conservo al cambiar Railway de US West a US East; no fue
  necesario cambiar el firmware.
- El criterio de cero perdida **no se cumple en el backend**: al cierre faltan
  cuatro identidades consecutivas, `seq=8..11`.

La indicacion visual `SD:OK` confirma disponibilidad del medio, pero no prueba
por si sola que esas cuatro identidades sigan presentes. El dictamen final de
almacenamiento local queda pendiente de leer fisicamente
`telemetry_audit_v1.jsonl`, `history_v1.csv` y, si existe,
`telemetry_v1.jsonl` despues de desenergizar el montaje.

## Causa tecnica probable

El firmware configura telemetria con `METER_MQTT_PUBLISH_QOS=0`. La funcion
`task_communication_publish_persisted()` elimina la primera linea de la cola
cuando `mqtt_client_publish()` devuelve `ESP_OK`. A su vez, esa funcion
considera exitoso el envio cuando el SIM7080G acepta el comando y el payload
de `AT+SMPUB`. Con QoS 0, esa aceptacion no demuestra recepcion por Mosquitto.

La coincidencia experimental es directa: el nodo reporto 51 publicaciones
aceptadas por el modem, pero el backend recibio solo 47. La hipotesis mas
probable es que `seq=8..11` fueron confirmadas localmente durante la transicion
inicial de la caida y retiradas antes de que el firmware reconociera que la
sesion con el broker ya no era util. Esta causa debe confirmarse contra los
archivos fisicos de la SD.

## Ajuste propuesto antes de repetir

1. Consultar `AT+SMPUB=?` en el modem real y guardar su respuesta. La
   documentacion SIMCom V1.07 declara soporte de QoS `0..2` y muestra
   `AT+SMPUB="information",5,1,1` como ejemplo QoS 1.
2. Configurar la telemetria en QoS 1, manteniendo modo MQTT sincrono.
3. Consultar `AT+SMSTATE?` inmediatamente antes de retirar una linea de la
   cola. Ante duda, conservarla: un duplicado con igual `boot_id+seq` es
   preferible a una perdida y el backend ya contabiliza duplicados/conflictos.
4. Mantener la identidad original en cada reintento y retirar el registro
   solamente despues del resultado sincronico QoS 1 y sesion valida.
5. Repetir primero un corte de 10 minutos y luego otro durante la transicion
   exacta de una publicacion. Criterio: 100 % de completitud eventual, cero
   conflictos y cero reinicios.

Una confirmacion de aplicacion desde el backend mediante un topic de ACK seria
la garantia mas fuerte, pero agrega comunicacion descendente y mas alcance. En
el plazo actual, QoS 1, confirmacion de sesion y conciliacion por identidad son
el ajuste proporcionado para la siguiente corrida.
