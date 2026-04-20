# Validacion ADE Recovery y Temperatura

## Objetivo

Validar en bancada que el sistema:

- recupera comunicacion con el ADE9153A sin reiniciar toda la ESP32-S3,
- enmascara la telemetria cuando el ADE queda invalido,
- vuelve a publicar metrologia real cuando el ADE reaparece,
- reporta temperatura del ADE de forma visible y con bandera de validez,
- no confunde datos basura del SPI con una reconexion real.

## Resultado observado (2026-03-14)

| Prueba | Resultado | Observacion |
|---|---|---|
| T1. Arranque normal con ADE conectado | PASS | `ADE_OK=1`, `TEMP_OK=1`, metrologia estable |
| T2. Falla fisica ADE en runtime | PASS | Sin reinicio de ESP32; entra recovery y mascara mediciones |
| T3. AC presente con ADE ausente temporalmente | PASS | Durante la falla se publican ceros utiles, no valores absurdos |
| T4. AC ausente con ADE sano | PASS | `AC=LOST` y magnitudes electricas en cero con ADE aun valido |
| T5. Recovery ADE tras volver a alimentar el chip | PASS | `SOFT_INIT` escala a `SPI_REINIT` y luego vuelve `ADE_OK=1` |
| T6. Temperatura ADE | PASS | `TEMP_OK=1` cuando ADE esta sano; `TEMP_OK=0` cuando el chip queda invalido |

## Patrones de log confirmados

### Durante la falla ADE

Se observaron patrones correctos como:

- `AVRMS invalid raw/plausibility - VRMS forced to 0.0V`
- `APERIOD invalid raw=0xffffffff - frequency forced to 0.0Hz`
- `TEMP_TRIM invalid gain=0xFFFF offset=0xFFFF`
- `medicion: AC=LOST ... TEMP_OK=0 ... ADE_OK=0`
- `ADE recovery requested`

Interpretacion:

- El firmware deja de confiar en el ADE.
- `Vrms`, `IrmsA`, `P`, `PF`, `F` y `E_hw` se enmascaran a cero.
- La temperatura tambien se enmascara (`TEMP_OK=0`).
- No se publican valores absurdos aunque aparezcan raws espurios.

### Durante la recuperacion

Se observaron secuencias correctas como:

- `ADE soft recovery failed - escalating immediately to SPI reinit`
- `ADE9153A recovery action=SPI_REINIT`
- `ADE recovery completed action=2`

Interpretacion:

- El recovery suave se intenta primero.
- Si el ADE sigue ausente o corrupto, el firmware escala a `SPI_REINIT`.
- Cuando el probe vuelve a ser valido, `ADE_OK` retorna a `1` y la metrologia sube gradualmente hasta su valor estable.

### Tras la recuperacion

Comportamiento observado:

- Vuelta gradual a valores como `33V -> 88V -> 123V` en unos pocos ciclos.
- Recuperacion de carga real sin reiniciar toda la ESP32-S3.
- `TEMP_OK=1` nuevamente cuando el ADE vuelve a entregar datos validos.

## Criterio de aceptacion

La validacion se considera aprobada para recovery runtime si se cumple todo esto:

- La ESP32-S3 no necesita power-cycle para recuperar ADE.
- Cuando el ADE falla, el sistema pasa a `ADE_OK=0` y no publica mediciones absurdas.
- Cuando el ADE vuelve, se observa `ADE recovery completed action=2` o equivalente y luego regresan mediciones reales.
- `TEMP_OK` solo vale `1` cuando la lectura de temperatura es coherente.

Estado actual: **APROBADO para recovery runtime del ADE9153A.**

## Notas practicas

- Los logs `oled_driver: OLED init commands failed` son esperados mientras la pantalla no este conectada.
- La temperatura del ADE puede mostrarse como `0.00C` con `TEMP_OK=0` durante la falla; eso es correcto y preferible a publicar una lectura basura.
- Como prueba opcional adicional, aun puede validarse el caso de arranque en frio con el ADE ausente desde el primer instante de power-up.
