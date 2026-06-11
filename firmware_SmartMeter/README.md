# firmware_SmartMeter

Firmware del medidor inteligente: ESP32-S3 + ADE9153A (metrología) +
SIM7080G (LTE-M/NB-IoT) + DS3231 (RTC) + MicroSD, sobre ESP-IDF v5.x con
FreeRTOS. Trabajo de grado, Maestría en Ingeniería de IoT — PUJ.

## Arquitectura

Capas estrictas: `drivers/` (acceso a hardware) → `modules/` (lógica de
dominio) → `tasks/` (orquestación FreeRTOS). Los datos compartidos viven en
`data/meter_data.c` protegidos por mutex; las tareas se comunican por colas
creadas en `tasks/task_manager.c`.

| Tarea | Core | Prio | Periodo | Responsabilidad |
|---|---|---|---|---|
| task_measurement | 0 | 5 | 250 ms | Lectura ADE9153A, energía acumulada en NVS, recuperación del bus SPI |
| task_power_quality | 1 | 4 | 250 ms | Detección sag/swell, flags PQ |
| task_calibration | — | 3 | event-driven | Calibración mSure, persistencia NVS |
| task_communication | 1 | 2 | 60 s | Sesión celular+MQTT, SenML, comandos remotos, backlog SD |
| task_ui | — | 1 | poll | OLED + teclado |
| task_supervisor | any | 6 | 5 s | Watchdog por software (ver abajo) |

## Tolerancia a fallas

Principio de diseño: la caída de un periférico degrada esa función, no el
nodo completo.

- **Arranque**: ningún init de periférico aborta (`app_main` registra la
  falla con `fault_handler` y sigue). ADE sin responder → modo degradado con
  mediciones enmascaradas hasta que el recovery lo recupere. RTC o SD
  ausentes → se pierde solo timestamping local / backlog offline.
- **Supervisión** (`modules/diagnostics/task_monitor.c`): cada tarea reporta
  latidos; el supervisor detecta tareas bloqueadas (p.ej. comunicación
  atascada en un AT), lo registra como `FAULT_TASK_STALL` y solo escala a
  `esp_restart()` si el bloqueo supera `METER_TASK_STALL_REBOOT_MS` (10 min).
  Umbrales por tarea en `config/meter_config.h`.
- **Backlog offline** (`drivers/sd_storage`): si MQTT cae, task_communication
  guarda una medición por minuto en `backlog.jsonl` (SenML con `bt` = epoch
  real); al recuperar sesión las drena en lotes de 8 por ciclo. El backend
  respeta el `bt` absoluto, así el histórico no queda con huecos.
- **Hora**: al boot se siembra el reloj del sistema desde el DS3231; al
  registrar en red se sincroniza vía `AT+CCLK` (sistema + RTC). Sin hora
  válida no se bufferiza (una muestra sin timestamp real es peor que
  perderla).

## Comandos remotos (topic `medidor/{id}/cmd`)

`reset`, `status`, `calibrate`, `probe`, `sync_time`, `relay_off`/`relay_on`
(pendientes del cableado del relé). Llegan como string plano desde el
backend, que los audita por usuario. Ver `task_communication_process_cmd()`.

## Telemetría (SenML RFC 8428)

- `medidor/{id}/datos` — V, I, P, Q, S, FP, f, energía, temperatura, flags PQ.
- `medidor/{id}/estado` — salud del nodo cada 5 min (retained).
- `medidor/{id}/alerta` — eventos PQ (event-driven).
- `medidor/{id}/conexion` — online/offline con LWT del broker.

## Compilación

PlatformIO (`pio run` en esta carpeta) o ESP-IDF directo:

```bash
idf.py set-target esp32s3
idf.py build
```

`platformio.ini` apunta a `sdkconfig.esp32s3_devkitc_1`. El componente
principal se registra en `main/CMakeLists.txt` — al agregar un `.c` nuevo
hay que listarlo ahí.

## Configuración

Todo lo ajustable está en `config/`:

- `board_config.h` — pines (ADE en SPI2, SD en SPI3 dedicado, I2C compartido
  OLED+RTC, UART1 para SIM7080G).
- `meter_config.h` — periodos, APN, umbrales de supervisión y backlog.
- `rtos_app_config.h` — prioridades, stacks y colas.
