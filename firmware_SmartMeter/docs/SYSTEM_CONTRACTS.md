# CONTRATOS COMUNES DEL SISTEMA

Este documento define la interfaz mínima usada por RTC, microSD, OLED,
SIM7080G, red celular, MQTT y, posteriormente, ADE9153A. El objetivo es que
cada módulo pueda fallar y recuperarse sin detener los demás.

## Estados

| Estado | Uso |
|---|---|
| `UNINIT` | El módulo todavía no ha sido inicializado. |
| `DISABLED` | El módulo fue omitido intencionalmente por configuración o perfil. |
| `RECOVERING` | Se está ejecutando un intento de recuperación. |
| `OK` | La última validación terminó correctamente. |
| `DEGRADED` | Existe una falla activa, pero aún no se agotaron los intentos rápidos. |
| `FAILED` | Se agotaron tres intentos consecutivos; continúa el sondeo lento. |

La política predeterminada usa tres intentos rápidos separados por 1, 2 y 4
segundos. Después se prueba de nuevo cada 60 segundos. SIM y ADE pueden definir
una política particular sin cambiar los estados públicos.

## API para los módulos

La interfaz pública está en `modules/system/system_health.h`.

Secuencia normal de inicialización:

```c
system_health_mark_recovering(SYSTEM_MODULE_RTC, "initializing");

if (rtc_init() == ESP_OK) {
    system_health_report_ok(SYSTEM_MODULE_RTC, "RTC ready");
} else {
    system_health_report_failure(SYSTEM_MODULE_RTC,
                                 SYSTEM_EVENT_INIT_FAILED,
                                 SYSTEM_SEVERITY_WARNING,
                                 ESP_FAIL,
                                 "RTC init failed");
}
```

Para un módulo omitido intencionalmente:

```c
system_health_set_disabled(SYSTEM_MODULE_ADE, "disabled by test profile");
```

Las ocurrencias que no representan una falla activa se publican con
`system_health_report_occurrence()`. La UI obtiene una copia consistente con
`system_health_get_snapshot()`; nunca consume la cola de eventos.

Cuando se repite la misma falla activa se incrementan los contadores del
snapshot, pero no se agrega otro evento a la cola. Una validación posterior
con `system_health_report_ok()` genera un evento `CLEARED` que conserva el
número de ocurrencias acumuladas.

## Tiempo

El tiempo interno siempre es UTC. Un epoch se considera válido solamente entre
2020-01-01 y 2099-12-31. La aplicación debe usar:

- `system_time_mark_valid()` después de validar RTC, red celular o ajuste remoto;
- `system_time_mark_invalid()` cuando no exista una fuente confiable;
- `system_time_get_status()` para consultar la validez sin depender del RTC.

Con hora inválida, los eventos conservan `timestamp_utc_s = 0`, `time_valid =
false`, uptime, `boot_id` y secuencia. La presentación local usa
`America/Bogota` (`-05:00`); nunca se inventa una fecha.

## Cola y despacho

Existe una única cola estática de 32 eventos y un único despachador:

- `INFO`: log serie y `events.jsonl` cuando la SD está disponible;
- `WARNING` y `CRITICAL`: log, `events.jsonl` y cola durable `alerts.jsonl` para
  envío MQTT;
- `CLEARED` de una alerta también se guarda y publica;
- si no hay SD, las alertas pendientes permanecen en una cola RAM limitada.

Si la cola central se llena, un `INFO` nuevo se descarta. Un `WARNING` o
`CRITICAL` sustituye el evento de menor prioridad permitido. El contador de
pérdidas aumenta y posteriormente se genera `EVENT_QUEUE_OVERFLOW`.

`task_communication` publica primero la telemetría viva y después retransmite
las alertas pendientes con QoS 1. Solo elimina una línea durable cuando MQTT
confirma que aceptó la publicación.

## Reinicios y aislamiento

Una recuperación de periférico no puede llamar a `esp_restart()`. Los únicos
reinicios automáticos admitidos son por bloqueo continuo de una tarea esencial
durante 10 minutos. Un reinicio remoto solicitado explícitamente se trata como
una operación de control, no como recuperación de periférico.

## Registros comunes

`system_record_base_t` contiene la cabecera compartida por mediciones, eventos,
telemetría pendiente y alertas pendientes: versión, dispositivo, `boot_id`,
secuencia, UTC, zona horaria, validez y uptime.

La política definida para la etapa de almacenamiento es:

- histórico de mediciones CSV cada 60 segundos;
- colas MQTT en JSONL;
- advertencia al 80 % y crítica al 90 % de ocupación;
- reserva del 5 % para alertas;
- eliminar primero históricos antiguos y, solo si es indispensable, telemetría
  pendiente antigua, generando `DATA_LOSS`;
- no eliminar alertas pendientes durante la limpieza normal.

La rotación y limpieza física de históricos se implementará al completar el
módulo de almacenamiento; sus límites ya forman parte del contrato.

## Perfil de autoprueba

El perfil `test_system_contracts` comprueba estados, `RAISED`, `CLEARED`,
deduplicación, tiempo válido/inválido, concurrencia desde dos tareas, saturación
de cola, prioridad y contador de descartes.

```powershell
pio run -e test_system_contracts -t upload --upload-port COM6
pio device monitor -p COM6 -b 115200
```

El resultado correcto contiene `HWTEST RESULT SYSTEM_CONTRACTS PASS`.

Validación física realizada el 2026-10-17 sobre el ESP32-S3 conectado en
`COM6`: compilación, carga y ejecución correctas. La salida confirmó
`PASS states dedup clear time overflow priority concurrency`.
