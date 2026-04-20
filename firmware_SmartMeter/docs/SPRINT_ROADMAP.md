# SPRINT ROADMAP

## Sprint 1 [IMPLEMENTADO]

- ADE9153A detection and base configuration.
- Core measurements (V, I, P, Q, S, PF, frequency).
- 1000 ms periodic measurement task.
- Shared data snapshot API with mutex protection.
- Estado actual: metrologia base validada funcionalmente en hardware real.

## Sprint 1.2 [IMPLEMENTADO / VALIDACION FINAL PENDIENTE]

- mSure calibration module and calibration task.
- NVS persistence for calibration constants.
- Basic power quality monitor (sag/swell, flags).
- Estado actual: la infraestructura existe, pero `CAL` sigue en `0x00` en pruebas recientes y falta cerrar la ruta operativa de calibracion en campo.

## Sprint 2 [IMPLEMENTADO / CIERRE DOCUMENTAL PENDIENTE]

- Unit tests and hardware validation bench.
- Accuracy validation target <= 1% in nominal range.
- Harmonics analysis implementation.
- Estado actual: V, I, P, Q, S, PF y frecuencia ya fueron probados con carga real, no-load y sin AC; armonicos sigue pendiente.

## Sprint 3 [VALIDADO EN HARDWARE]

- SIM7080G integration (NB-IoT/LTE-M).
- MQTT uplink.
- Data serialization pipeline.
- Runtime ADE9153A recovery without rebooting the ESP32-S3.
- Estado actual: conexion, reconexion, power-cycle del SIM7080G, estabilidad MQTT, telemetria real y recovery runtime del ADE ya quedaron probados con PASS.
- Nota: los warnings de OLED en logs son esperados mientras la pantalla no este conectada.

## Sprint 4 [BASE IMPLEMENTADA / VALIDACION HARDWARE PENDIENTE]

- OLED display integration.
- Button navigation and menu system.
- UI task with event-driven refresh.
- Estado actual: la base de software existe (`task_ui`, `display_manager`, `menu_handler`, keypad), pero falta validacion fisica con la OLED y los botones conectados.

## Sprint 5 [NO INICIADO]

- AC/DC and battery power control logic.
- Power management task.
- Backup and fault power transitions.
- Estado actual: `task_power_mgmt` sigue en stub y los pines de control aun estan comentados.
