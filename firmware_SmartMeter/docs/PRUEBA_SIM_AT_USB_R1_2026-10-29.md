# Prueba aislada SIM_AT por USB — 29 de octubre de 2026

## Propósito

Comprobar que el SIM7080G responde por UART a comandos AT, reconoce la SIM y reporta señal e IMEI, sin iniciar MQTT ni incluir ADE o microSD.

## Montaje y versión

- Alimentación del ESP32: USB.
- AC: desconectada.
- Perfil: `test_sim_at_usb`, firmware `1.1.0-simat-usb-r1`.
- El perfil intenta inicializar y actualizar la OLED; no acciona PWRKEY.
- La compilación y la carga a COM6 terminaron correctamente; esptool verificó los datos escritos.

## Resultado observado

La captura de COM6, entre las 14:02:12 y las 14:04:14 (hora de Colombia), contiene **9 ciclos completos consecutivos con resultado PASS**. En cada ciclo:

- `ATI`: el módem responde (`R1951.01 OK`).
- `AT+CPIN?`: SIM lista (`READY`).
- `AT+CSQ`: `29,99` en las nueve muestras.
- IMEI leído: `860016040126034`.

Esto confirma respuesta básica del módem y de la interfaz UART durante el intervalo observado. No mide registro LTE, PDP, apertura TCP ni sesión MQTT y, por tanto, no explica por sí solo las caídas MQTT. El valor CSQ tampoco sustituye las lecturas `CPSI/CESQ` para evaluar RSRP, RSRQ y SINR.

## Evidencia y límites

- Log: `logs/device-monitor-261029-140210.log`.
- Snapshot del firmware: `evidence/versions/2026-10-29_simat-usb-r1-build/`.
- La prueba comenzó antes de la primera línea conservada; el tiempo registrado de 2 min 1 s corresponde a la ventana capturada, no necesariamente al arranque completo.
