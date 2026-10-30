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
- `telemetry_audit_v1.jsonl`: copia permanente de cada paquete de medicion;
- `alerts_v1.jsonl`: alertas pendientes;
- `events_v1.jsonl`: eventos locales.

El perfil `test_sd_service` sustituye el directorio por
`/sdcard/codex_sd_test` y no formatea la tarjeta.

El encabezado CSV v1 es:

```text
schema,device,boot_id,sequence,timestamp_utc,local_bogota,time_valid,uptime_ms,type,vrms,irms_a,p_w,q_var,s_va,pf,f_hz,e_wh,temp_c,pq
```

Desde el firmware `1.1.0-obj4`, el historico de 60 s genera tambien el paquete
SenML con la misma identidad y muestra. Se escribe en la auditoria permanente
y en la cola pendiente. Un contador exclusivo de mediciones evita confundir
eventos del sistema con huecos de entrega. La auditoria conserva exactamente
el paquete enviado, incluso despues de vaciar la cola.

La telemetria nueva se guarda como array SenML. Los registros antiguos y las
alertas envueltos en JSON con `payload` siguen siendo legibles. Con hora no
confiable se conserva `time_valid=false`, `ts_device=0` e identidad por
dispositivo, boot y secuencia. No se inventa una fecha de respaldo.

## Confirmacion y recuperacion

Comunicaciones lee una copia RAM de la primera linea. Solo despues de recibir
exito de MQTT solicita confirmacion; el servicio reescribe el remanente en
`.tmp`, renombra el original a `.bak`, instala el nuevo archivo y elimina el
respaldo. Tras un corte, el arranque conserva el archivo principal valido o
restaura `.bak`; si solo existe `.tmp`, lo promueve.

Una confirmacion encolada bloquea nuevas lecturas de esa cabeza hasta que se
procese, para evitar que un segundo ACK elimine el registro siguiente.
La confirmacion significa aceptacion por el modem; la recepcion real en el
backend se verifica con la conciliacion SD-BD, segun `OBJ4_TELEMETRY_VALIDATION.md`.

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

## Bus SPI compartido con ADE9153A

La PCB comparte SPI2 entre el ADE9153A y la microSD. La seleccion es
independiente:

- ADE9153A: CS en GPIO10.
- microSD: CS en GPIO7.

`drivers/spi_shared` es el unico propietario de la inicializacion del bus. Los
drivers de ADE y SD solicitan el bus ya creado y no vuelven a ejecutar
`spi_bus_initialize`. Durante el arranque, ambos CS se colocan inactivos antes
de registrar cada dispositivo. Esta arquitectura elimina el error de doble
inicializacion sin recablear la PCB.

La microSD se opera a **4 MHz**. En la PCB montada, 20 MHz produjo un
`ESP_ERR_TIMEOUT` transitorio; el servicio se desmonto, reintento y recupero en
aproximadamente un segundo mientras el ADE permanecio operativo. A 4 MHz, la
prueba de humo posterior escribio una medicion de 60 s y dreno la cola sin
repetir el timeout.

El resultado actual es una mitigacion funcional. El criterio de cierre es una
corrida integrada de al menos 60 minutos sin error SPI persistente, seguida de
conciliacion SD-BD de 30 registros y un corte de red de 10 minutos con drenado
completo del backlog.
