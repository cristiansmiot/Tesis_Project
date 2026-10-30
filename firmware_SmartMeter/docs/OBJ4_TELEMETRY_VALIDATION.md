# Validacion de telemetria para el Objetivo 4

Fecha de corte: **24 de octubre de 2026**  
Firmware: `1.1.0-obj4`  
Dispositivo: `SM-348518435344`  
Plataforma: commit `9fca7d1`, desplegado en Railway

## 1. Estado de los bloqueadores B06-B09

| Bloqueador | Implementacion | Evidencia al corte | Estado |
|---|---|---|---|
| B06 Trazabilidad | `/datos` contiene `bn`, `boot_id`, `seq`, `ts_device`, `time_valid`, `uptime_ms`, `fw` y `cal`; backend conserva `ts_server` | Corrida 12 h: un solo `boot_id`, 713 secuencias consecutivas, 713/713 con hora valida | **Cerrado funcionalmente** |
| B07 Radio | `/estado` conserva RSSI, RSRP, RSRQ, RSSNR crudo, SINR y `radio_valid` desde `AT+CPSI?` | RSRP -91 a -84 dBm; RSRQ -20 a -13 dB; SINR -14 a 4 dB durante 12 h | **Cerrado funcionalmente** |
| B08 Contadores | Generacion, intentos, aceptaciones del modem, sesiones y reconexiones son contadores separados | Los contadores avanzaron con la publicacion y registraron una reconexion sin confundirla con mensajes | **Cerrado funcionalmente** |
| B09 Copia local | La misma identidad se escribe en historico CSV, auditoria JSONL, cola MQTT y BD | Prueba de humo ADE+SD aprobada a 4 MHz; falta una corrida con corte de red y conciliacion completa | **Implementado; cierre experimental pendiente** |

## 2. Identidad y fuente de verdad

La clave experimental de una medicion es:

```text
device_id + boot_id + seq + record_type
```

Cada lectura debe generar los siguientes artefactos bajo `smartmeter/`:

- `history_v1.csv`: variables electricas y clave de muestra.
- `telemetry_audit_v1.jsonl`: copia permanente del payload generado.
- `telemetry_v1.jsonl`: cola pendiente de publicacion.

La auditoria SD es la fuente de verdad para contar mensajes generados. Una
aceptacion `SMPUB/OK` solo demuestra que el modem acepto la publicacion; la
recepcion extremo a extremo se confirma por la presencia de la misma clave en
la base de datos. Los duplicados conservan la primera hora de llegada e
incrementan su contador; un mismo ID con contenido distinto se registra como
conflicto y no sustituye silenciosamente la muestra original.

## 3. Arquitectura SPI compartida

- Bus: SPI2 compartido por ADE9153A y microSD.
- CS ADE9153A: GPIO10.
- CS microSD: GPIO7.
- Ambos CS permanecen altos al inicializar el bus y cada dispositivo usa su
  transaccion independiente.
- La inicializacion se centralizo en `drivers/spi_shared` para evitar
  `spi_bus_initialize: SPI bus already initialized`.
- El reloj de la microSD se fijo en **4 MHz** para aumentar el margen de
  integridad de señal en la PCB montada.

Durante la integracion se observo un `ESP_ERR_TIMEOUT` transitorio de lectura
SD que se recupero automaticamente en aproximadamente un segundo. El ADE no
perdio operacion. Tras bajar de 20 MHz a 4 MHz, la prueba de humo genero la
medicion de 60 s y dreno `telemetry_v1.jsonl` sin repetir el timeout. Esto es
evidencia de mitigacion, no aun una demostracion de estabilidad prolongada.

## 4. Contadores y tiempo

| Campo `/estado` | Significado |
|---|---|
| `data_generated_count` | Mediciones generadas durante el boot |
| `data_publish_attempts` | Intentos de publicar `/datos`, incluidos reintentos |
| `data_publish_ok` | Publicaciones aceptadas por el modem, incluidos reenvios |
| `mqtt_sessions_ok` | Sesiones MQTT establecidas |
| `mqtt_ok_cnt` | Alias legacy de sesiones; no cuenta datos |
| `reconnect_count` | Sesiones exitosas menos la sesion inicial |

`ts_device` y `bt` representan segundos UTC de generacion. `ts_server` es la
primera llegada al callback MQTT del backend. Con `time_valid=false`, el
registro conserva identidad y uptime, pero no participa en el calculo de
latencia.

La repeticion RTC de aproximadamente 24 horas fue funcionalmente aprobada:
`RTC:OK`, `TIME:OK`, hora correcta y fuente `RTC` antes de conectar el SIM. El
voltaje reportado paso de 3.290 V a 3.267 V. Sigue pendiente cuantificar deriva
contra una referencia y cerrar la seguridad electrica de la ruta de respaldo.

## 5. Resultado basal de 12 horas

La prueba se ejecuto sin microSD para aislar la estabilidad del nodo y las
comunicaciones. La ventana exacta fue del 23 de octubre de 2026 a las 21:38:50
al 24 de octubre a las 09:38:50, hora de Bogota.

| Indicador | Resultado |
|---|---:|
| Mediciones / secuencias | 713 / `1..713` |
| PDR de generados | 100 % |
| Cobertura nominal | 99.03 % |
| Huecos / duplicados / conflictos | 0 / 0 / 0 |
| Reinicios | 0 |
| AC / ADE disponibles | 100 % / 100 % |
| Latencia p50 / p95 / p99 | 14.937 / 15.432 / 21.797 s |
| Latencia maxima | 47.411 s |
| Reconexiones MQTT | 1 |
| Incidentes MQTT | 2, ambos despejados |

Los incidentes MQTT duraron aproximadamente 8 s y 108 s. El segundo produjo
el intervalo maximo de 127 s entre lecturas. No hubo huecos dentro de las
muestras generadas, pero la corrida sin SD no puede demostrar que una lectura
intermedia haya sido adquirida durante la pausa.

## 6. Plataforma desplegada

Railway tiene en linea frontend, backend, PostgreSQL y broker Mosquitto. El
frontend esta conectado a `main` y al directorio `energia-iot-frontend`; el
despliegue contiene el commit `9fca7d1`.

La plataforma distingue alarmas activas de eventos despejados, conserva inicio
y fin y muestra el historico en la ventana temporal seleccionada. Para el
escenario residencial se configuraron 120 V nominales y una banda 108-132 V.
La alarma de subtension/sobretension requiere persistencia de al menos 60 s y
confirmacion por lecturas consecutivas.

## 7. Prueba inmediata para cerrar B09

### 7.1 Puerta G1: una hora integrada

1. Conectar ADE, SD, RTC, OLED, teclado y SIM.
2. Arrancar con AC protegida y sin carga.
3. Registrar hora, `boot_id`, firmware, espacio SD y contadores iniciales.
4. Dejar 60 minutos sin manipular.
5. Confirmar al menos 59 muestras validas, cero reinicios y cero fallos SPI
   persistentes.
6. Probar una vez UP, DOWN, SELECT y BACK, registrando que no haya eventos
   duplicados.

### 7.2 Conciliacion de 30 registros

Seleccionar 10 registros del inicio, 10 del centro y 10 del final. Para cada
clave, comparar SD, API/BD y dashboard. Criterio: 30/30 presentes, mismos
valores dentro del redondeo declarado y cero conflictos.

### 7.3 Corte de red de 10 minutos

1. Conservar AC, ADE y SD activos.
2. Interrumpir de forma segura el acceso de red o el broker; no retirar la
   antena mientras el modem transmite.
3. Registrar inmediatamente `AT+CEREG?`, `AT+CNACT?`, `AT+SMSTATE?`,
   `AT+CPSI?`, etapa de recuperacion, duracion y ultimo error de publicacion.
4. Comprobar que la secuencia y la auditoria SD continuaron.
5. Restablecer el servicio y esperar `backlog_count=0`.
6. Ejecutar:

```powershell
python tools/reconcile_obj4.py --sd RUTA/telemetry_audit_v1.jsonl --db RUTA/mediciones.json
```

Criterio: completitud eventual 100 %, perdida neta cero, cero conflictos y
duplicados identificables.

## 8. Consultas y evidencia

```text
GET /api/v1/salud/SM-348518435344/estabilidad?desde=<UTC>&hasta=<UTC>
GET /api/v1/eventos/?device_id=SM-348518435344&desde=<UTC>&hasta=<UTC>&limit=500
GET /api/v1/mediciones/?device_id=SM-348518435344&skip=0&limit=10000
```

Para la corrida oficial, guardar el payload de configuracion, exportaciones,
archivos SD, captura de la plataforma, log serial y hashes SHA-256. No borrar
la base de datos ni las alarmas para obtener una ventana limpia: aislar cada
ensayo por tiempo, `boot_id`, firmware y `run_id`.
