# Resumen Estado Actual SmartMeter (ADE9153A + ESP32-S3 + SIM7080G)

Fecha de corte: 2026-03-14

## 1) Estado general

- La metrologia base ya esta operativa y validada en hardware real para `V`, `I`, `P`, `Q`, `S`, `PF` y frecuencia.
- El pipeline SIM7080G + MQTT ya quedo validado en pruebas reales: conexion inicial, reconexion, estabilidad y publicacion continua.
- El power-cycle del SIM7080G tambien quedo validado: al apagar y volver a encender el modulo, la ESP32 sigue viva y la sesion MQTT se recupera.
- La recuperacion runtime del ADE9153A ya quedo validada: si el chip pierde alimentacion o la respuesta SPI se vuelve invalida, el firmware enmascara la telemetria y recupera el ADE sin reiniciar toda la ESP32.
- Los warnings de OLED observados en logs no son un bug funcional del firmware; corresponden a que la pantalla no estaba conectada durante estas pruebas.

## 2) Estado real por sprint

- Sprint 1: implementado y validado funcionalmente.
- Sprint 1.2: implementado; `mSure`, persistencia NVS y `task_calibration` existen, pero falta cerrar la validacion operativa de calibracion en campo. En las pruebas actuales `CAL` sigue en `0x00`.
- Sprint 2: metrologia validada en banco propio con carga, sin carga y sin AC. Si se quiere cierre formal, queda pendiente una tabla de error contra instrumento de referencia y armonicos.
- Sprint 3: validado en hardware real para SIM7080G, MQTT, telemetria y recovery runtime del ADE9153A.
- Sprint 4: base de software implementada (`task_ui`, `display_manager`, `menu_handler`, teclado), pero falta validacion hardware con OLED y botones conectados.
- Sprint 5: no iniciado funcionalmente; `task_power_mgmt` sigue en stub.

## 3) Evidencia reciente observada en pruebas (2026-03-14)

- Pruebas de metrologia satisfactorias con AC presente, sin AC, sin carga y con diferentes cargas.
- `vrms`, `irms_a`, `p_w`, `q_var`, `s_va`, `pf` y `f_hz` se comportaron de forma coherente con la carga aplicada.
- Pruebas del SIM7080G satisfactorias en conexion inicial, reconexion, power-cycle del modulo, estabilidad sin perdida permanente del broker y publicacion continua de telemetria real.
- En broker se observaron publicaciones sostenidas al variar carga, lo que confirma que `task_communication`, `data_serializer` y `mqtt_client` estan trabajando en conjunto.
- En fallas del ADE se observaron `ADE_OK=0` y `TEMP_OK=0` con magnitudes electricas en cero, sin publicar valores absurdos.
- Al volver a alimentar el ADE, se observo `ADE recovery completed action=2` y regreso progresivo a metrologia real sin reiniciar la ESP32.

## 4) Interpretacion corta de los campos mas relevantes

- `q_var`: potencia reactiva en VAR. En la convencion actual puede crecer en magnitud negativa al aumentar la carga.
- `s_va`: potencia aparente en VA. Se reporta como magnitud positiva y aumenta con la carga.
- `e_soft_wh`: energia integrada por software desde `p_w`.
- `e_nvs_wh`: energia total persistida en NVS.
- `e_wh`: alias legacy del total publicado; hoy equivale al total persistido.
- `CAL`: mascara de estado de calibracion. Si permanece en `0x00`, la metrologia puede funcionar con constantes por defecto, pero no hay una calibracion mSure valida/cargada reportada.
- `ade_ok`: `1` cuando el ADE esta validado y sus datos se consideran confiables.
- `temp_ok`: `1` cuando la temperatura del ADE es valida; `0` cuando la lectura se enmascara por falla o implausibilidad.

## 5) Conclusiones tecnicas de esta ronda

- El problema principal reportado antes en runtime para el ADE9153A quedo resuelto a nivel funcional.
- La telemetria ya no acepta lecturas basura del ADE como si fueran validas; primero marca falla, enmascara datos y luego exige reconfirmacion del chip.
- El comportamiento observado al quitar AC tambien es el esperado: la deteccion cae a `AC=LOST`, limpia magnitudes electricas y conserva operativo el resto del firmware.
- El modem SIM7080G se comporta bien en las pruebas descritas por el usuario; cualquier warning intermitente de heartbeat quedo cubierto por la logica de recovery y no impidio retomar MQTT.

## 6) Siguientes pasos recomendados

1. Cerrar formalmente la documentacion de Sprint 2 y Sprint 3 con las evidencias ya obtenidas.
2. Validar en hardware la OLED y los botones para cerrar Sprint 4.
3. Definir la convencion final de telemetria si se quiere migrar luego a SenML/RFC 8428.
4. Cerrar la ruta de calibracion mSure en campo para dejar `CAL` con evidencia distinta de `0x00`.
5. Como prueba opcional adicional, validar el caso de arranque en frio con el ADE ausente desde power-up.

## 7) Ubicacion concreta del estado en codigo

- Arranque y validacion ADE: `main/main.c`, `drivers/ade9153a/ade9153a_init.c`
- Recovery y enmascaramiento de metrologia: `tasks/task_measurement.c`
- Estado de calibracion `CALIB_STATUS_*`: `tasks/task_calibration.h`
- Telemetria JSON publicada al broker: `modules/communication/data_serializer.c`
- UI Sprint 4: `tasks/task_ui.c`, `modules/ui/display_manager.c`, `modules/ui/menu_handler.c`
- Power management Sprint 5: `tasks/task_power_mgmt.c`
