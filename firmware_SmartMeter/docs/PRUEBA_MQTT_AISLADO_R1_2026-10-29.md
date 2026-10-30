# Prueba MQTT aislada R1 — 29 de octubre de 2026

## Propósito y montaje

Verificar durante 30 minutos la publicación MQTT QoS 1 del firmware experimental
`1.1.0-mqtt-r1` y observar su recuperación ante una degradación del SIM7080G.

- Perfil: `test_sim_mqtt`; ESP32-S3 por COM6.
- Alimentación: USB únicamente; entrada AC desconectada.
- ADE9153A y microSD: excluidos por el perfil.
- Inicio del intervalo válido: 10:25:53, primer `MQTT_ISO CONNECT PASS`.
- Fin del intervalo: 10:55:53, tras 30 minutos.
- Commit base: `842a3fd`; binario SHA-256: `AE83445857C65CA10BD27C288B46D05E7D6B64019CD68B9F9024079A788D6272`.
- Registro serie completo: `../evidence/mqtt-isolation-r1-20261029.log`; SHA-256: `85600BBD0DCA6A42068056483ED677BAFC740896CFD6A8A2C10C8AF49BA3C3B4`.

## Resultado

**No cumple el criterio de estabilidad de 30 minutos.** La sesión operó y
publicó de forma sostenida durante la mayor parte del intervalo, pero perdió la
comunicación antes de completar los 30 minutos y no volvió a confirmar MQTT
dentro del tiempo de prueba.

| Indicador | Resultado observado |
|---|---:|
| Publicaciones confirmadas por `SMPUB` | 26 |
| Publicaciones intentadas | 27 |
| Publicaciones fallidas | 1 |
| Confirmaciones entre intentos realizados | 96,3 % (26/27) |
| Confirmaciones respecto a los ~30 envíos esperados a cadencia nominal de 60 s | 86,7 % (26/30) |
| Envíos esperados no intentados tras perder la sesión | 3 |
| Latencia de las 26 publicaciones confirmadas | media 1 507,5 ms; mediana 1 547,5 ms; p95 1 553 ms |
| Rango de latencias confirmadas | 991–1 554 ms |
| Desconexiones recuperadas antes del corte | 0 |
| Recuperaciones MQTT confirmadas después del fallo | 0 |

La proporción corresponde a confirmaciones que informó el módem al ejecutar
`SMPUB`. Este perfil no verifica la recepción por un suscriptor ni la inserción
en PostgreSQL; no debe presentarse como tasa de entrega extremo a extremo. Los
30 envíos esperados son una estimación a partir de la cadencia de 60 s y la
ventana de 30 min; el programa no genera identificadores para los intervalos en
que ya no dispone de sesión.

## Secuencia observada

1. Tras cargar el perfil, el módem necesitó seis intentos de conexión fallidos.
   Hubo varios periodos sin respuesta AT; posteriormente respondió, registró
   LTE Cat-M1 y MQTT conectó en el intento 7, a las 10:25:53.
2. Se confirmaron 26 publicaciones QoS 1 consecutivas. La mayoría tardó entre
   1,54 y 1,55 s.
3. A las 10:52:57, `seq=27` falló con `ESP_ERR_TIMEOUT` después de 23 658 ms.
   Las consultas de diagnóstico `CEREG`, `CNACT` y `SMSTATE` también agotaron
   tiempo. A las 10:53:13 el equipo ya no pudo confirmar la sesión; la
   recuperación de red terminó con `ESP_ERR_TIMEOUT` y etapa `network_failed`.
4. A las 10:54:43 volvió el registro celular (`CEREG=5`). El contexto PDP
   aparecía activo (`CNACT=1`, con dirección IP), pero MQTT seguía desconectado
   (`SMSTATE=0`) y `SMCONN` respondió `+CME ERROR: operation not allowed`.
5. La corrección R1 sí ejecutó un ciclo PDP: confirmó el contexto inactivo y
   logró reactivarlo en aproximadamente 2,14 s. Sin embargo, los intentos
   `SMCONN` posteriores siguieron devolviendo `operation not allowed`; el
   límite de un ciclo por minuto suprimió nuevos ciclos durante esa ventana.
   Al terminar los 30 minutos no había un nuevo `MQTT_ISO CONNECT PASS`.

## Interpretación y decisión

La prueba confirma que R1 detecta el rechazo `operation not allowed` y puede
reiniciar el contexto PDP de forma verificable. **No demuestra recuperación
MQTT:** tras recuperar el PDP, el módem todavía rechazó `SMCONN`, y el perfil no
registró una reconexión antes del corte. Por tanto, R1 queda como versión
experimental fallida para el criterio de estabilidad, no como versión estable.

La pérdida inicial de respuesta AT y el timeout de `SMPUB` antecedieron al
`operation not allowed`. Esos eventos apuntan primero a una pérdida temporal de
respuesta del módem; con este ensayo no se puede atribuir la causa física a
alimentación, UART, radio o firmware. La señal no puede compararse durante el
intervalo de falla porque el módem dejó de responder a las consultas AT.

## Siguiente paso recomendado

Antes de otra corrida de 30 minutos, ajustar la recuperación para medir qué
pasa después de reactivar el PDP: esperar el tiempo de estabilización previsto,
volver a consultar `SMSTATE` y reconfigurar/reintentar MQTT en el siguiente
ciclo. Registrar cada intento con la etapa, `CEREG`, `CNACT`, `SMSTATE`, código
de publicación y tiempo desde el ciclo PDP. No afirmar que la publicación fue
recibida en el backend sin contrastar el broker o la base de datos.
