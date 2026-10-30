# Observación prolongada de red celular sin MQTT — 29 de octubre de 2026

## Objetivo

Determinar si el patrón informado de sesiones MQTT cercanas a 30 minutos y
periodos sin conexión de aproximadamente 30 minutos también aparece en el
registro celular o en el contexto PDP, manteniendo MQTT fuera de la prueba.

## Montaje y configuración

- ESP32-S3 por USB, COM6; entrada AC desconectada.
- Perfil `test_sim_network_long`, firmware `1.1.0-netdiag-r1`.
- ADE y operaciones de microSD excluidos por el perfil. No se hicieron
  publicaciones MQTT ni conexiones TCP a Railway.
- SIM7080G, SIM, antena y ubicación del equipo permanecieron iguales durante el
  intervalo válido.
- Registro serie: `../evidence/sim-network-long-20261029.log`.
- Binario SHA-256:
  `B100184FDEF27B8A1F2FDE55F910673C881A1B06AA397DEC82D65E96014F0AF2`.

## Inicio válido y resultado

El primer periodo se interrumpió cuando se pulsó manualmente PWRKEY y el módem
se apagó. El registro marcó `NETDIAG DROP` a las 11:24:28.762; ese evento fue
provocado intencionalmente y no se cuenta como caída espontánea. Tras volver a
encender el módem, el registro y PDP quedaron activos a las 11:29:34.341. Este
es el inicio del intervalo válido.

El último muestreo válido quedó registrado a las 12:40:07.334. La duración
monotónica fue 4 233 070 ms (70 min 33,07 s). No hubo reinicios del ESP32,
caídas espontáneas de registro ni pérdida del contexto PDP durante ese
intervalo.

| Métrica | Resultado |
|---|---:|
| Muestras de señal `AT+CSQ` | 281 |
| `CSQ` mínimo / máximo / promedio | 26 / 30 / 28,84 |
| Capturas de registro, operador, PDP y radio | 15 de cada comando |
| `CEREG` | `0,5` en todas las capturas: registrado en roaming |
| Operador reportado | Movistar; modo de acceso LTE Cat-M1 |
| Contexto PDP | Activo en todas las capturas; se mantuvo la misma celda reportada |
| Caídas espontáneas `NETDIAG DROP` | 0 |
| Fallos del perfil en el intervalo válido | 0 |

`CSQ` se reporta en la escala nativa del módem y no se convierte aquí a dBm.
El `CEREG=5` aislado confirma roaming, pero no demuestra que el roaming sea la
causa de las fallas MQTT. El periodo de 70 minutos tampoco descarta una
interrupción celular menos frecuente ni una falla intermitente posterior.

## Interpretación

En esta corrida, la capa de registro celular y el contexto PDP se conservaron
durante más de una hora, por lo que no se reprodujo el patrón de cortes en esa
capa. Esto reduce la probabilidad de que una pérdida de registro o una
desactivación PDP periódica explique por sí sola las desconexiones MQTT
descritas en el análisis previo.

La prueba no abrió conexiones TCP, no alcanzó el proxy de Railway y no ejecutó
MQTT. Por tanto, no descarta problemas de DNS, ruta IP del operador, NAT,
transporte TCP, proxy de Railway, cliente MQTT del módem o sincronización entre
el firmware y la sesión del módem. El análisis anterior sobre los logs del
broker es información aportada para orientar el diagnóstico, no una medición
repetida en este ensayo.

## Siguiente prueba

Probar periódicamente una conexión TCP directa al proxy público del broker,
sin enviar un paquete MQTT ni publicar mensajes. Registrar éxito/resultado de
`CAOPEN`, estado con `CASTATE`, cierre con `CACLOSE`, duración, y estado de
registro/PDP/radio en los fallos. Si TCP falla mientras registro y PDP siguen
sanos, la investigación se centra en DNS/ruta/transporte o proxy. Si TCP se
mantiene, el foco pasa al cliente MQTT AT y su manejo de sesión y publicación.
