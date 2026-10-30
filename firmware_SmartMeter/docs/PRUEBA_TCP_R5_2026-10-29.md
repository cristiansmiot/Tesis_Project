# Ensayo TCP R5 del SIM7080G — 29 de octubre de 2026

## Propósito

Repetir el diagnóstico de socket TCP tras seleccionar explícitamente `CACID=0`, usando USB Serial/JTAG como consola principal para asegurar la captura por COM6. La variante no utiliza MQTT, ADE ni microSD; AC permaneció desconectada.

## Resultado de preparación

La compilación terminó correctamente en 239,65 s. Se cargó `1.1.0-tcpdiag-r5` a COM6 y esptool verificó los hashes escritos. El binario y los archivos de configuración/fuente quedaron guardados en `evidence/versions/2026-10-29_tcpdiag-r5-build/`.

## Resultado del ensayo

El monitor serial se mantuvo abierto durante más de tres minutos, pero no recibió caracteres. El archivo `logs/device-monitor-261029-133924.log` quedó con cero bytes. Luego se conectó COM6 antes de un reinicio controlado solo del ESP32; tampoco se capturó salida en `logs/device-monitor-261029-134457.log`. Finalmente se probó COM6 sin filtros y se repitió el reinicio controlado; tampoco hubo texto serial. No se obtuvo el inicio del perfil, la respuesta a `CACID` ni un resultado de `CAOPEN`; por tanto, el ensayo R5 es **inconcluso** y no se contabiliza como un intento TCP fallido.

## Antecedente que sí quedó medido

En R2 y R3 `CAOPEN` devolvió inmediatamente `+CME ERROR: operation not allowed`, mientras el módem reportaba `CEREG=0,5` y el PDP activo. Eso no es un resultado TCP de DNS, rechazo o timeout y no permite adjudicar la causa al broker ni al operador. El motivo de la ausencia de salida serial en R5 aún debe aclararse antes de continuar con la hipótesis de ciclos de una hora.

## Siguiente paso

Confirmar que el ESP32 arrancó con el perfil R5 observando la OLED. No pulsar PWRKEY ni RESET hasta identificar si el firmware está ejecutándose. Si la OLED no permite identificarlo, capturar primero el arranque por el canal UART0 o revisar el estado de alimentación USB; no iniciar todavía una corrida MQTT prolongada.

## Evidencia

- Registro de monitor (vacío): `logs/device-monitor-261029-133924.log`.
- Registro de arranque controlado (vacío): `logs/device-monitor-261029-134457.log`.
- Monitor sin filtros: COM6 no entregó datos durante la sesión ni después del reinicio controlado.
- Versión cargada: `evidence/versions/2026-10-29_tcpdiag-r5-build/`.
- Resultados de R2 y R3: `docs/PRUEBA_TCP_R3_2026-10-29.md` y `evidence/sim-tcpdiag-r2-20261029.log`, `evidence/sim-tcpdiag-r3-20261029.log`.

## Reejecución TCP R5 con SIM previamente validada — 29 de octubre de 2026

Antes de repetir el perfil TCP se ejecutó `test_sim_at_usb`: nueve ciclos completos de `ATI`, `CPIN`, `CSQ` e IMEI pasaron por COM6. Después se compiló de nuevo y se cargó `1.1.0-tcpdiag-r5`; la carga a COM6 terminó correctamente y esptool verificó los hashes. El binario de esta compilación se conserva en `evidence/versions/2026-10-29_tcpdiag-r5-rebuild/`.

El monitor no pudo abrir COM6: Windows devolvió `PermissionError(13, Acceso denegado)`; una apertura directa con pySerial tuvo el mismo resultado. No se obtuvo ninguna línea del perfil TCP, así que este intento sigue **inconcluso** y no cuenta como fallo de TCP. La placa permanece alimentada solo por USB, AC desconectada. El usuario informó que el SIM tiene PWR encendido y NET parpadeando tras accionar PWRKEY.

**OLED apagada es lo esperado en `test_sim_tcp`: este perfil no inicializa la pantalla.** No es evidencia de que la OLED se haya dañado ni confirma que el perfil TCP haya pasado. El siguiente paso es liberar COM6 cerrando cualquier monitor serial que lo tenga abierto (incluido el monitor integrado en VS Code/PlatformIO), mantener el cable USB conectado y volver a intentar capturar la consola. No iniciar todavía una corrida MQTT prolongada.
