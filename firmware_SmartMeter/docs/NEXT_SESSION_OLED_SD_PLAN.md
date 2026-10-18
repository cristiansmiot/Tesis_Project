# Plan de siguiente sesion: microSD y OLED

## Objetivo

Dejar la microSD y la OLED listas para el firmware principal bajo FreeRTOS,
sin integrar todavia el ADE9153A. Cada modulo debe fallar y recuperarse de
forma independiente, reportar su estado mediante los contratos comunes y no
provocar reinicios del ESP32.

La prioridad de implementacion es:

1. servicio de almacenamiento microSD;
2. integracion de eventos y telemetria con la microSD;
3. servicio OLED basado en snapshots;
4. pruebas separadas;
5. prueba conjunta OLED + RTC + microSD.

## Estado comprobado al iniciar

- Hardware: ESP32-S3 DevKitC-1.
- OLED SSD1306: direccion `0x3C`, SDA GPIO8, SCL GPIO9. Prueba individual y
  conjunta aprobada, sin parpadeos ni reinicios.
- microSD: MOSI GPIO11, MISO GPIO13, SCLK GPIO12 y CS GPIO7. Tarjeta de 8 GB,
  FAT, identificada como `SA08G` con aproximadamente 7386 MB. Prueba de
  escritura y relectura de 16 registros aprobada.
- RTC DS3231: responde en `0x68` y el servicio de tiempo funciona. La retencion
  por bateria queda pendiente por una CR2032 agotada.
- La bateria retirada midio 1.394 V. El portabateria vacio y la PCB alimentada
  midieron 3.074 V, confirmando una ruta de carga en el modulo DS3231 (resistor
  `201` y diodo). Mantener el portabateria vacio. Antes de usar otra CR2032 se
  debe abrir esa ruta de carga y despues repetir la prueba de retencion.
- El RTC ahora conserva `OSF`, comprueba `EOSC`, rechaza fechas no confiables y
  reporta `RTC_BACKUP_LOST`. Sin bateria, `TIME` debe quedar degradado o fallido
  sin afectar OLED, SD ni ESP32.
- Contratos comunes, cola de eventos y snapshots ya existen en
  `modules/system/`.
- El perfil `test_all_no_ade` compilo correctamente despues de los cambios
  RTC. La compilacion posterior de `test_system_contracts` fue interrumpida de
  forma intencional al preparar este traspaso; debe repetirse junto con
  `test_oled_rtc_service`.
- El arbol de trabajo ya contiene cambios del usuario y del desarrollo previo.
  No limpiar, revertir ni reemplazar archivos ajenos a este plan.

## Decisiones de arquitectura

### microSD

Separar dos niveles:

- `sd_storage`: controlador FAT/SDSPI de bajo nivel. Montaje, desmontaje,
  operaciones de archivo, consulta de capacidad y recuperacion de temporales.
- `storage_service`: unica tarea RTOS autorizada para escribir y mantener los
  archivos. Los productores envian solicitudes por una cola estatica y nunca
  esperan una operacion larga de FAT.

El servicio debe:

- usar memoria estatica y limites de longitud definidos;
- montar sin formatear automaticamente;
- marcar `SD` como `RECOVERING`, `OK`, `DEGRADED` o `FAILED`;
- aplicar 3 reintentos con esperas de 1, 2 y 4 segundos y despues reintentar
  cada 60 segundos;
- detectar errores de lectura/escritura, invalidar el montaje y remontar sin
  reiniciar el ESP32;
- serializar todas las operaciones en una sola tarea;
- utilizar archivos temporales y reemplazo recuperable para el drenado de las
  colas. En el arranque debe resolver `.tmp`/respaldo incompleto sin perder el
  archivo valido;
- conservar las alertas por encima de historicos y telemetria pendiente.

No se debe permitir que `task_communication`, el despachador de eventos u otra
tarea escriban directamente con `fopen` o llamen una reescritura larga. Durante
la migracion pueden conservarse adaptadores publicos compatibles.

### OLED

`task_ui` debe ser el unico propietario del controlador OLED. La pantalla:

- consulta snapshots de salud y tiempo, pero no consume eventos;
- no accede directamente al modem, RTC ni SD;
- refresca con periodo limitado y mantiene la UI estable aunque falte un
  modulo;
- presenta `--` o el estado real, nunca datos inventados;
- intenta reinicializar la OLED con la politica comun de recuperacion;
- si falla el I2C, reporta el fallo y deja vivas las demas tareas;
- evita limpiar y redibujar innecesariamente para reducir parpadeo.

La prioridad visual sera: alerta critica activa, advertencia activa y despues
pantalla normal. El estado de ADE debe mostrarse como `--`/`DISABLED`.

## Formatos de almacenamiento

Crear las siguientes finalidades logicas. Los nombres concretos pueden quedar
en `meter_config.h`, pero no deben dispersarse como literales:

- historico CSV de mediciones, periodo de 60 segundos;
- cola JSONL de telemetria MQTT pendiente;
- cola JSONL de alertas pendientes;
- registro JSONL de eventos locales, si no duplica la cola de alertas.

Todos los registros deben incluir:

- version de esquema;
- identificador del dispositivo;
- `boot_id`;
- secuencia;
- epoch UTC;
- fecha/hora Bogota con `-05:00` cuando sea valida;
- `time_valid`;
- uptime;
- tipo de registro.

Si la hora no es valida, guardar `timestamp=0`, `time_valid=false`, uptime,
`boot_id` y secuencia. No usar el ano 1970, 2000 ni otra fecha sustituta.

El CSV debe escribir el encabezado una sola vez y usar un orden versionado de
columnas. La cola JSONL debe mantener un objeto completo por linea y rechazar
lineas truncadas o mayores al limite acordado.

## Politica de espacio

- Menos de 80 % usado: `SD OK`.
- Desde 80 %: `SD_LOW_SPACE`, severidad `WARNING`.
- Desde 90 %: `SD_FULL`, severidad `CRITICAL`.
- Liberar primero historicos antiguos.
- Si sigue siendo indispensable, eliminar telemetria pendiente mas antigua y
  emitir `DATA_LOSS` con el numero de registros/bytes eliminados.
- No eliminar automaticamente alertas pendientes.
- Mantener una reserva configurable para poder almacenar alertas.

La prueba de estos umbrales no debe llenar fisicamente los 8 GB. Introducir una
fuente de capacidad o umbrales inyectables solamente en el perfil de prueba.

## Ejecucion propuesta

### Fase 1 — Cerrar la base antes de modificar SD/OLED

1. Ejecutar `git diff --check` sin modificar cambios ajenos.
2. Compilar:

   ```powershell
   pio run -e test_system_contracts -e test_oled_rtc_service -e test_all_no_ade
   ```

3. Corregir cualquier incompatibilidad causada por `RTC_BACKUP_LOST` o el
   nuevo campo `rtc_eosc_set`.
4. No cargar un firmware de sincronizacion RTC: actualmente no hay bateria.

Criterio: los tres perfiles compilan y ninguna ruta de recuperacion de
perifericos llama a `esp_restart()`.

### Fase 2 — Contrato y tarea de almacenamiento

1. Crear `modules/storage/storage_service.h/.c` y, si conviene,
   `tasks/task_storage.h/.c`.
2. Definir solicitudes estaticas: historico, telemetria pendiente, alerta y
   evento. Definir claramente que ocurre si la cola se llena.
3. Añadir snapshot de almacenamiento: montaje, capacidad, espacio libre,
   porcentaje usado, escrituras exitosas/fallidas, pendientes y perdidas.
4. Integrar estados y eventos comunes de `SYSTEM_MODULE_SD`.
5. Implementar montaje, invalidacion, desmontaje seguro y remontaje.
6. Recuperar archivos temporales en el arranque.
7. Añadir la tarea al `task_manager` sin convertirla en tarea esencial capaz
   de reiniciar el equipo.

Criterio: retirar/fallar la SD degrada solo almacenamiento; la tarea de tiempo,
OLED, comunicaciones y el ESP32 siguen funcionando.

### Fase 3 — Historico y colas

1. Implementar el registro CSV cada 60 segundos.
2. Migrar el backlog de telemetria para usar el servicio.
3. Migrar persistencia de alertas del despachador al servicio.
4. Implementar drenado por lotes pequeños y confirmacion antes de eliminar.
5. Implementar politica 80/90 %, purga y `DATA_LOSS`.
6. Asegurar que una falla o espera de SD no bloquee `task_communication`.

Criterio: un corte durante la actualizacion de una cola conserva al menos una
copia valida y los registros confirmados no se publican dos veces salvo la
duplicacion tolerada e identificable por `boot_id` + secuencia.

### Fase 4 — Perfil de prueba microSD de servicio

Agregar `test_sd_service` en `platformio.ini` y `hardware_test.h/.c`.

Debe probar:

1. montaje y capacidad;
2. creacion de estructura y encabezado CSV;
3. escritura y relectura de registros con hora invalida;
4. escritura y relectura con hora valida inyectada;
5. cola JSONL y drenado parcial/completo;
6. recuperacion de `.tmp` simulado;
7. concurrencia de varios productores;
8. saturacion de la cola de solicitudes;
9. umbrales 80/90 % inyectados;
10. contadores y eventos de error;
11. funcionamiento durante al menos 30 minutos sin corrupcion ni reinicio.

El perfil no debe borrar archivos del usuario ni formatear la tarjeta. Usar un
directorio de prueba claramente identificado y limpiar solamente archivos
creados por ese perfil cuando sea seguro.

### Fase 5 — OLED de servicio

1. Revisar `task_ui`, `display_manager` y `oled_driver`.
2. Centralizar un snapshot de vista para que todas las filas correspondan al
   mismo instante.
3. Mostrar como minimo: hora/`SIN FECHA`, SD, RTC/TIME, CEL, MQTT, ADE
   deshabilitado y alerta principal.
4. Añadir indicacion de backlog/espacio SD sin saturar la pantalla.
5. Evitar llamadas de driver fuera de `task_ui` en el firmware normal.
6. Comprobar que un fallo de render genera un evento repetido sin inundar la
   cola y que una recuperacion genera `CLEARED`.

Criterio: pantalla estable, sin parpadeos visibles; un fallo OLED no bloquea ni
reinicia las tareas restantes.

### Fase 6 — Prueba conjunta sin ADE

Crear o actualizar un perfil `test_oled_rtc_sd_service` y despues validar
`test_all_no_ade`.

Secuencia de banco:

1. mantener fuera la bateria RTC y confirmar que `TIME` muestre no valido sin
   impedir escritura SD;
2. observar OLED + RTC + SD durante 30 minutos;
3. comprobar que historico y contadores avanzan;
4. apagar con seguridad, retirar la SD, arrancar y confirmar degradacion sin
   reinicio;
5. apagar, reinstalar la SD y confirmar recuperacion tras arrancar;
6. con MQTT disponible, provocar una caida de comunicacion, acumular backlog y
   verificar drenado al recuperar;
7. revisar la tarjeta en el PC: CSV legible, JSONL valido, sin `.tmp` huérfanos
   ni archivos truncados;
8. ejecutar una prueba continua de al menos 2 horas y posteriormente una de
   24 horas.

No retirar ni insertar el adaptador microSD con la PCB energizada salvo que se
haya implementado y validado expresamente hot-plug. Para las pruebas iniciales,
apagar antes de tocarlo.

## Archivos principales a revisar

- `drivers/sd_storage/sd_storage.c/.h`
- `modules/system/system_contracts.h`
- `modules/system/system_health.c`
- `modules/system/system_event_dispatcher.c`
- `modules/communication/data_serializer.c/.h`
- `tasks/task_communication.c`
- `tasks/task_manager.c/.h`
- `drivers/oled/oled_driver.c/.h`
- `modules/ui/display_manager.c/.h`
- `tasks/task_ui.c/.h`
- `config/meter_config.h`
- `config/rtos_app_config.h`
- `main/hardware_test.c/.h`
- `platformio.ini`
- `docs/HARDWARE_TEST_RESULTS.md`

## Entregables de la siguiente sesion

- servicio RTOS de almacenamiento con escritor unico;
- formatos CSV/JSONL documentados y versionados;
- recuperacion y politica de espacio implementadas;
- OLED consumiendo snapshots y mostrando estados reales;
- nuevos perfiles de prueba compilados;
- resultados de banco registrados en `docs/HARDWARE_TEST_RESULTS.md`;
- lista corta de pruebas fisicas que deba ejecutar el usuario;
- ninguna dependencia funcional del ADE9153A.

## Texto para iniciar la otra sesion

> Continua el proyecto desde `docs/NEXT_SESSION_OLED_SD_PLAN.md`. Revisa primero
> el estado real del arbol de trabajo y no reviertas cambios existentes. Ejecuta
> la Fase 1 y despues implementa las fases de microSD y OLED en orden, usando
> FreeRTOS, memoria estatica, contratos comunes y aislamiento de fallas. El ADE
> debe permanecer deshabilitado. No uses una bateria en el modulo DS3231 hasta
> desactivar su ruta de carga. Compila y deja preparados los perfiles; solicita
> mi intervencion solo cuando sea necesaria una prueba fisica.
