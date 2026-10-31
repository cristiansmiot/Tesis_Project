# Metrología en banco (Objetivo 4: P01, P02, P03)

Herramientas para medir el medidor (DUT) contra el PCR500MA, el Keysight 34450A y el Fluke 43B, siguiendo el protocolo `Documentos/Protocolo_Caracterizacion_Objetivo4_v0.2.md`.

| Archivo | Función |
|---|---|
| `ejecutar_escenario.py` | Aplica escalones de tensión y frecuencia en el PCR. Registra el PCR y el 34450A, y el Fluke si se pide, con hora UTC y etiqueta de escalón. |
| `analizar_metrologia.py` | Cruza el histórico de la microSD con las referencias. Calcula error, MAE, IC 95 %, U y la decisión por punto. |
| `escenarios/*.csv` | Escenarios listos: humo, P01 tensión, P01 frecuencia, P01 no lineal, P03 sin carga, PQ, y una plantilla de P02. |
| `referencias.json` | Límites del protocolo y especificaciones de los instrumentos (editable; hay que poner el certificado si existe). |
| `instrumentos.py` | Controladores del PCR, el 34450A y el Fluke 43B, más sus simuladores. |
| `grabar_respaldo.cmd` | Graba un binario respaldado sin recompilar (conserva la NVS). |
| `test_metrologia.py` | Prueba de extremo a extremo con banco simulado: `python -m unittest test_metrologia -v`. |

## Firmware

Se usa la variante `metrology_sd`, versión `1.1.0-metro-r1`:

- **Activo:** ADE9153A, RTC, OLED, NVS y microSD.
- **Apagado:** celular y MQTT. No gasta datos y no envía alertas.
- **Histórico en la SD:** cada **5 s**, para reunir 30 pares por punto en 2,5 min.
- **Fuente de verdad del DUT:** `smartmeter/history_v1.csv` en la microSD.
- **Hora:** si el RTC no tiene hora válida, el análisis alinea por `uptime_ms` usando el preámbulo de sincronía. Si la tiene, reporta su error frente al PC.

Para grabarla (con la AC desconectada y el USB al PC):

```
grabar_respaldo.cmd ..\..\evidence\versions\2026-10-31_metro-r1-build COM6
```

Para volver a la `mqtt-r2` validada (SHA-256 `B1EBD713…`):

```
grabar_respaldo.cmd ..\..\evidence\versions\2026-10-31_mqtt-r2-respaldo-binario COM6
```

Después de la r2 hay que encender el SIM7080G con su botón: los perfiles de prueba no pulsan PWRKEY.

## Seguridad (el ADE9153A no está aislado)

1. **Nunca** conecte el USB del medidor al PC con AC aplicada. Durante el ensayo, el medidor se alimenta con un cargador de pared.
2. El medidor se conecta **solo** a la salida del PCR, nunca a la red.
3. Los cambios de carga se hacen con la salida apagada. El ejecutor apaga el PCR en cada `cambio_carga` y espera ENTER.
4. Para leer la SD, primero se desenergiza el medidor (se desconecta el cargador) y después se retira la tarjeta.

## Montaje

```
PCR500MA L ──┬─────────────── borne V(L) del medidor
             ├── 34450A V (paralelo, modo ACV)        Fluke: punta V en paralelo
PCR500MA N ──┴── borne V(N) ── canal de corriente del medidor ── CARGA ── retorno
                                    (pinza del Fluke en este conductor)
```

- **Modo ACV** (por defecto): el 34450A mide V y f en paralelo. Usa el rango de 100 V hasta 115 V y el de 750 V por encima.
- **Modo ACI** (`--dmm-modo ACI --dmm-rango 1`): el 34450A va **en serie** con la carga (borne A) y mide I. En este modo la referencia de V pasa a ser el PCR.
- **Fluke 43B:** use el modo *Volts/Amps/Hz* para V e I, y *Power* para P y FP. Con corrientes pequeñas, pase el conductor N vueltas por la pinza y analice con `--fluke-espiras N`.

## Uso

```
cd C:\Tesis_Project\firmware_SmartMeter\tools\metrology
python ejecutar_escenario.py escenarios\00_humo.csv --carga "LED OSRAM 15 W" --temp 24
python ejecutar_escenario.py escenarios\P01_tension.csv --carga "LED OSRAM 15 W" --fluke COM8
```

- Cada corrida queda en `firmware_SmartMeter/evidence/obj4_metrologia/<prueba>_<fecha>_runNN/`.
- Al inicio, el ejecutor agrega un **preámbulo de sincronía** de unos 2 min: 120, 110, 130, 115 y 120 V.
- `--fluke COM8` requiere `pyserial` (`python -m pip install pyserial`). La alternativa es correr `Fluke43BLogger.exe` en paralelo y pasar su CSV con `--fluke` al análisis.

Para analizar, apague el medidor, copie la SD y ejecute:

```
python analizar_metrologia.py ..\..\evidence\obj4_metrologia\P01_tension_20261031_run01 --sd D:\smartmeter\history_v1.csv
```

Los resultados quedan en `<corrida>/resultados/`:

- `resumen.md`
- `puntos.csv` y `pares.csv`
- `resultados.xlsx`
- los gráficos de series, errores con ±U y Bland-Altman

## Qué se puede concluir con las cargas disponibles

| Prueba | LED 15 W y cargador | Observación |
|---|---|---|
| P01 frecuencia (58–62 Hz) | Sí | La referencia es la frecuencia secundaria del 34450A y la consigna del PCR. Con U de unos 0,01 Hz frente al límite de 0,10 Hz, la prueba es concluyente. |
| P01 tensión | Sí, con matiz | En el rango de 100 V (≤115 V), U ≈ 0,3 %. En el rango de 750 V (120–132 V), U ≈ 0,95 %, así que el resultado sale *indeterminado* salvo que el error sea casi nulo o haya un certificado del 34450A. |
| P01 carga no lineal | Comparación | Con unos 0,13 A, el PCR aporta ±0,02 A (≈15 %) y ±(2 % + 0,5 W) (≈5 % a 15 W), y la pinza del Fluke no resuelve (marcó P = 0 W). En modo ACI con el rango de 1 A, U ≈ 1,5 %. El FP se reporta como observado. |
| P03 sin carga | Sí | 30 min a 120 V con el circuito de corriente abierto. |
| P01 resistiva (5 niveles), inductiva, P02 y P04 | No | Requieren cargas resistivas de 60 a 400 W (el PCR entrega como máximo ~4 A a 120 V) y una carga inductiva (motor o ventilador). |
