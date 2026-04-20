# GUIA DE CALIBRACION

## Fases del ciclo de vida (AN-1571 Fig.1)

- Arquitectura: definir rango nominal, sensores y constantes objetivo.
- Produccion: ejecutar autocalibracion mSure y programar correcciones de ganancia.
- Campo: re-chequeo periodico y recalibracion ante deriva.

## Constantes Objetivo (supuestos EV board)

- `IMAX = 10 A`
- `VNOM = 220 V`
- `AI_PGAGAIN = 16`
- `RSHUNT = 1 mOhm`
- `RSMALL = 1 kOhm`
- `RBIG = 1 MOhm`

Formulas (AN-1571):

- `TARGET_AICC [nA/code] = IMAX * AIHEADROOM * 52,725,703`
- `TARGET_AVCC [nV/code] = VNOM * VHEADROOM * 26,362,852`
- `TARGET_WCC  [uW/code] = TARGET_AICC * TARGET_AVCC / 2^27`
- `TARGET_WHCC [nWh/code] = TARGET_WCC * 2^13 / (3600 * 4000)`

## Por que esperar 8 segundos antes de leer CERT

La certeza mSure (`MS_ACAL_AICERT`/`MS_ACAL_AVCERT`) se estabiliza tras la convergencia interna.  
Usar menos de 8 s puede producir certeza falsa y programacion de ganancia incorrecta.

## Interpretacion de CERT

- CERT se expresa en ppm.
- `3000 ppm = 0.3%` de incertidumbre.
- Criterio tipico de aceptacion en este proyecto: `CERT <= 3000 ppm`.

## Formula de Ganancia

Para el canal `X`:

`XGAIN = (MS_ACAL_XCC / TARGET_XCC - 1) * 2^27`

Aplicar la ganancia signed resultante en registros de ganancia ADE (`AIGAIN`, `AVGAIN`, etc.).

## Disparadores de Recalibracion

| Condicion | Accion |
|---|---|
| Primer arranque tras manufactura | Calibracion completa |
| Reemplazo de sensor | Calibracion completa |
| Degradacion sostenida de CERT | Re-ejecutar canal afectado |
| Actualizacion de firmware que afecta metrologia | Validacion + recalibracion opcional |

## Disparo de Calibracion (Sprint 1.2)

Flujo previsto:

1. `task_manager_request_calibration(request_type)`.
2. `task_calibration` consume la solicitud desde la cola.
3. La maquina de estados mSure corre no bloqueante basada en ticks.
4. Las ganancias actualizadas se persisten en NVS y se recargan en arranque.

## Evidencia minima de mSure funcionando

En serial deben aparecer estas lineas para una corrida valida:

- `mSure done: mode=... AICC=... AICERT=... AVCC=... AVCERT=... AIGAIN=... AVGAIN=...`
- sin `msure_run failed`
- sin `mSure certainty invalid`

Al reiniciar, si hay persistencia valida:

- `calibration loaded from NVS: mode=... AIGAIN=... AVGAIN=... AICERT=... AVCERT=...`

