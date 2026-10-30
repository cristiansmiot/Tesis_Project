# Prueba TCP R7 del SIM7080G — 29 de octubre de 2026

## Objetivo

Separar la resolución DNS de la apertura TCP al proxy Mosquitto. R7 resuelve
`shinkansen.proxy.rlwy.net` desde el módem y usa la IP obtenida en cinco
intentos TCP, separados por 60 s. La prueba no envía MQTT ni telemetría y no
usa ADE ni microSD.

## Condiciones

- Placa alimentada por USB; AC desconectada durante todo el ensayo.
- Perfil `test_sim_tcp`; firmware `1.1.0-tcpdiag-r7` cargado y verificado en
  COM6.
- El usuario había confirmado que la OLED estaba encendida.
- SIM7080G con PWR encendido; captura por COM6 entre 16:03 y 16:07, hora local.
- Destino TCP: proxy público de Railway, puerto 58954.

## Hallazgos

1. El módem resolvió el nombre correctamente:
   `shinkansen.proxy.rlwy.net` → `66.33.22.231`.
2. La DNS del módem reportó `CDNSPDPID=4` y los servidores IPv4
   `80.58.61.250` y `80.58.61.254`.
3. El registro celular se mantuvo en roaming (`CEREG=0,5`), LTE Cat-M1 en
   línea y PDP activo (`CNACT: 0,1,"10.66.42.214"`; `CGACT: 1,1`). La señal
   observada fue aproximadamente RSRP −80 a −84 dBm, RSRQ −12 a −14 dB y
   SINR 11 a 13 dB.
4. Los cinco `CAOPEN` a la IP numérica `66.33.22.231:58954` terminaron en
   314 ms con `+CME ERROR: operation not allowed`. En todos los casos
   `CASTATE=0,0`, `CACID=0` y `SMSTATE=0`.
5. El perfil terminó automáticamente tras cinco intentos: `opened=0`,
   `failed=5`. No hubo una respuesta `+CAOPEN: 0,<resultado>` ni confirmación
   del socket.

## Conclusión

La resolución DNS funciona y no explica el rechazo. La misma respuesta
inmediata observada en R6 ocurre al conectar directamente a la IP, por lo que
el problema está en la apertura TCP o en sus condiciones internas, antes de
que el módem informe un resultado de conexión. Esta evidencia no confirma si
algún paquete SYN salió del SIM7080G ni si Railway recibió el intento; no
permite culpar al broker, al operador o al proxy.

El manual SIMCom V1.07 documenta `AT+CAOPEN` con los parámetros usados y
publica el rango de CID, PDP, tipo y puerto admitidos. La [página oficial del
SIM7080G](https://www.simcom.com/product/SIM7080G.html) incluye el manual en
la sección de descargas. Por ello, no conviene seguir repitiendo el mismo
intento a esa IP sin cambiar una variable.

## Siguiente prueba recomendada

Usar una segunda dirección TCP conocida del mismo entorno Railway —el dominio
público del backend por el puerto 443— y limitarla a tres aperturas con cierre
explícito si alguna funciona. Si esa conexión abre, la interfaz TCP del módem
funciona y la diferencia se concentra en el proxy o el puerto 58954. Si también
es rechazada en milisegundos, el siguiente paso será contrastar el contexto PDP
y las restricciones del SIM/operador con un segundo endpoint, antes de
reactivar MQTT de producción.

## Evidencia

- Log serie: `evidence/sim-tcpdiag-r7-20261029.log`.
- Binario, fuentes y configuración: `evidence/versions/2026-10-29_tcpdiag-r7-dns-test/`.
- SHA-256 del binario: `D6112F90BE4DDD006FD78C8B5782FA6AA0C2F84E09828D12A8DEC85B8EB631E6`.
- Tamaño: 495 888 bytes; RAM: 78 260 / 327 680 bytes; flash: 495 481 /
  1 048 576 bytes.
- Revisión mostrada: `842a3fd-dirty`; el árbol tenía cambios locales.
- La ejecución de R7 finalizó sola y el monitor se cerró. No se modificó
  Railway, Mosquitto ni PostgreSQL.

