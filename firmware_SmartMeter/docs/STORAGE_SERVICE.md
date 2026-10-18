# Servicio de almacenamiento microSD

## Propiedad y aislamiento

`modules/storage/storage_service.c` es la unica tarea del firmware normal que
opera `sd_storage`. Productores como comunicaciones y el despachador de eventos
usan colas estaticas y retornan inmediatamente. Las pruebas standalone de
hardware pueden llamar el driver de bajo nivel antes de arrancar el servicio.

La tarea monta sin formatear, recupera archivos `.tmp`/`.bak`, serializa las
operaciones FAT y aplica reintentos a 1, 2 y 4 segundos; despues prueba cada
60 segundos. Un error de E/S desmonta la tarjeta y degrada solamente
`SYSTEM_MODULE_SD`.

## Archivos versionados

Los archivos de produccion quedan en `/sdcard/smartmeter`:

- `history_v1.csv`: historico cada 60 segundos;
- `telemetry_v1.jsonl`: telemetria MQTT pendiente;
- `alerts_v1.jsonl`: alertas pendientes;
- `events_v1.jsonl`: eventos locales.

El perfil `test_sd_service` sustituye el directorio por
`/sdcard/codex_sd_test` y no formatea la tarjeta.

El encabezado CSV v1 es:

```text
schema,device,boot_id,sequence,timestamp_utc,local_bogota,time_valid,uptime_ms,type,vrms,irms_a,p_w,q_var,s_va,pf,f_hz,e_wh,temp_c,pq
```

Cada linea JSONL pendiente contiene `v`, `device`, `boot`, `seq`, `ts_utc`,
`local`, `time_valid`, `uptime_ms`, `type` y `payload`. Si la hora no es
confiable, `ts_utc` vale `0`, `local` queda vacio y `time_valid` es `false`.
Nunca se sintetiza una fecha de 1970 o 2000.

## Confirmacion y recuperacion

Comunicaciones lee una copia RAM de la primera linea. Solo despues de recibir
exito de MQTT solicita confirmacion; el servicio reescribe el remanente en
`.tmp`, renombra el original a `.bak`, instala el nuevo archivo y elimina el
respaldo. Tras un corte, el arranque conserva el archivo principal valido o
restaura `.bak`; si solo existe `.tmp`, lo promueve.

## Espacio y prioridad

- menos de 80%: `SD OK`;
- desde 80%: `SD_LOW_SPACE` (`WARNING`);
- desde 90%: `SD_FULL` (`CRITICAL`) y modo reserva;
- tambien se entra en reserva si quedan 64 MB o menos libres;
- en modo reserva se detienen nuevas escrituras de historico y telemetria;
- cada ciclo de capacidad elimina primero un lote acotado de filas historicas
  antiguas, conservando el encabezado CSV mediante reemplazo `.tmp`/`.bak`;
- solo cuando ya no quedan filas historicas se elimina telemetria pendiente
  antigua en lotes pequenos. La linea reclamada por comunicaciones se conserva
  para no competir con una publicacion en curso;
- cada purga de telemetria emite `DATA_LOSS` con registros y bytes eliminados y
  actualiza los contadores del snapshot;
- alertas y eventos conservan prioridad y pueden usar la reserva configurable;
- las alertas pendientes nunca se purgan automaticamente.

## Cola llena

Hay una cola prioritaria estatica para alertas/confirmaciones y otra normal
para telemetria/eventos. Si una cola se llena, la solicitud retorna
`ESP_ERR_NO_MEM`, aumenta `requests_dropped` y el productor sigue ejecutando.
El despachador conserva alertas en su cola RAM si no puede persistirlas.
