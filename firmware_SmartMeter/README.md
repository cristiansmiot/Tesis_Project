# firmware_SmartMeter

Firmware para medidor inteligente basado en ESP32-S3 + ADE9153A sobre ESP-IDF v5.x.

## Alcance Sprint 1

- Bring-up ADE9153A por SPI2_HOST (FSPI).
- Tarea periodica de medicion cada 1000 ms (`vTaskDelayUntil`).
- Datos compartidos protegidos por mutex (`meter_data`).
- Modulos de medicion base (V, I, P, Q, S, PF) y diagnostico (temperatura/fallas).
- Stubs documentados para sprint 1.2+.

## Organizacion del proyecto

- `main/`: componente principal ESP-IDF (fuentes reales del firmware).
- `config/`, `drivers/`, `modules/`, `tasks/`, `data/`, `utils/`: capas de arquitectura.
- `src/`: componente placeholder para compatibilidad PlatformIO (sin `app_main`).
- `platformio.ini`: configuracion de PlatformIO para ESP32-S3 con framework ESP-IDF.

## Compilacion en PlatformIO (VSCode)

1. Abrir esta carpeta como proyecto: `firmware_SmartMeter`.
2. Ejecutar `PlatformIO: Build` o `pio run`.
3. Flashear con `PlatformIO: Upload`.
4. Ver logs con `PlatformIO: Monitor`.

## Compilacion directa con ESP-IDF

```bash
idf.py set-target esp32s3
idf.py build
```

