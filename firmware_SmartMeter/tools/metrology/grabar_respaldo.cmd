@echo off
REM Graba un binario respaldado (bootloader + particiones + app) sin recompilar,
REM para que el SHA-256 cargado coincida con el registrado en VERSIONADO.
REM La NVS (0x9000) no se toca: se conservan calibracion y energia acumulada.
REM
REM Uso: grabar_respaldo.cmd <carpeta_con_bins> [COM6]
REM   metrologia: ..\..\evidence\versions\2026-10-31_metro-r1-build
REM   mqtt-r2:    ..\..\evidence\versions\2026-10-31_mqtt-r2-respaldo-binario
setlocal
set "CARPETA=%~1"
set "PUERTO=%~2"
if "%PUERTO%"=="" set "PUERTO=COM6"
if not exist "%CARPETA%\firmware.bin" (
  echo No existe "%CARPETA%\firmware.bin"
  exit /b 1
)
echo.
echo  AC DESCONECTADA del medidor? El shield ADE9153A no es aislado.
echo  Puerto %PUERTO%  -  binario "%CARPETA%\firmware.bin"
echo.
pause
certutil -hashfile "%CARPETA%\firmware.bin" SHA256
"%USERPROFILE%\.platformio\penv\Scripts\python.exe" "%USERPROFILE%\.platformio\packages\tool-esptoolpy\esptool.py" ^
  --chip esp32s3 --port %PUERTO% --baud 460800 --before default_reset --after hard_reset ^
  write_flash --flash_mode dio --flash_size 8MB --flash_freq 80m ^
  0x0 "%CARPETA%\bootloader.bin" 0x8000 "%CARPETA%\partitions.bin" 0x10000 "%CARPETA%\firmware.bin"
