# Sprint 2 - Protocolo de Pruebas (Validacion y Precision)

## 1. Objetivo

Validar en hardware real que el firmware cumple Sprint 2:

- estabilidad funcional del pipeline de medicion,
- persistencia de energia en NVS,
- exactitud metrologica objetivo `<= 1%` en rango nominal,
- base de evidencias para iniciar armonicos.

## 2. Variables a registrar por muestra

En serial, conservar estas columnas:

- `AC`, `Vrms`, `IrmsA`, `P`, `PF`, `F`
- `E` (total persistido), `E_soft`, `E_hw`
- `RAW_VRMS`, `RAW_EHI`
- timestamp (ms del log)

## 3. Equipos minimos

- DUT (ESP32-S3 + ADE9153A).
- red AC estable.
- cargas resistivas conocidas (bombillos 1/2/3).
- multimetro TRMS o medidor de referencia (ideal: analizador de energia clase mejor que DUT).
- PC para captura serial.

## 4. Seguridad

- no manipular conexiones con AC energizada.
- usar proteccion y aislamiento adecuados.
- no exceder corriente nominal de shunt/placa/cableado.

## 5. Preparacion previa

1. Flashear firmware actual.
2. Reiniciar DUT y confirmar que inicia sin fault.
3. Verificar en vacio:
   - `AC=LOST` con red desconectada.
   - `RAW_VRMS` bajo (ruido base esperado).
4. Conectar AC sin carga:
   - `AC=PRESENT`
   - `IrmsA ~ 0`, `P ~ 0`.

## 6. Matriz de pruebas (funcional)

Ejecutar cada estado por `60 s` minimo (recomendado `120 s`):

1. Sin AC.
2. AC sin carga.
3. AC + 1 bombillo.
4. AC + 2 bombillos.
5. AC + 3 bombillos.
6. Retirar 1 bombillo (volver a 2).
7. Retirar todos (AC sin carga).
8. Retirar AC.

Para cada paso, registrar:

- promedio de `Vrms`, `IrmsA`, `P`, `PF`, `F`,
- desvio estandar de `P`,
- delta de `E_soft`, `E_nvs`, `E_hw`,
- tiempo de transicion `AC LOST->PRESENT` y `PRESENT->LOST`.

## 7. Prueba de persistencia NVS (energia)

1. Con carga activa por al menos `120 s`.
2. Anotar `E_nvs` antes de reinicio.
3. Reiniciar equipo.
4. Anotar `E_nvs` despues de reinicio (sin carga).

Criterio:

- `E_nvs(post)` debe ser aproximadamente igual a `E_nvs(pre)` (tolerancia sugerida `+-0.01 Wh`).

## 8. Prueba de validacion mSure

Objetivo: confirmar que la calibracion mSure se ejecuta (o se reaplica) y queda valida.

### 8.1 Evidencias en log

- Inicio del task:
  - `task_calibration started on core 1`
- Reaplicacion desde NVS:
  - `calibration loaded from NVS: ... AICERT=... AVCERT=...`
- Ejecucion mSure en vivo:
  - `mSure done: mode=... AICC=... AICERT=... AVCC=... AVCERT=... AIGAIN=... AVGAIN=...`
- Fallas a revisar:
  - `persisted calibration record invalid - skipping apply`
  - `msure_run failed: ...`
  - `mSure certainty invalid: ...`

### 8.2 Criterios de aceptacion

- `AICERT <= 3000` y `AVCERT <= 3000` (ppm).
- No deben aparecer errores de `msure_run failed` en una ejecucion valida.
- Debe quedar persistencia de calibracion en NVS (log de `calibration loaded from NVS` al reiniciar).

### 8.3 Estado de calibracion en telemetria

El log de medicion incluye `CAL=0x..` (bitmask):

- `0x01` RUNNING
- `0x02` VALID
- `0x04` PERSISTED
- `0x08` LOADED
- `0x80` ERROR

Valores esperados:

- calibracion exitosa en vivo: normalmente incluye `VALID|PERSISTED|LOADED` (`0x0E`)
- reaplicacion de NVS valida: normalmente `0x0E`

## 9. Prueba de exactitud (Sprint 2)

Para cada nivel de carga (1, 2, 3 bombillos):

1. Medir referencia externa (`V_ref`, `I_ref`, `P_ref`, ideal `E_ref`).
2. Capturar promedio DUT en la misma ventana temporal.
3. Calcular error relativo:

`error_% = abs(DUT - REF) / REF * 100`

Criterio Sprint 2:

- `|error_V| <= 1%`
- `|error_I| <= 1%`
- `|error_P| <= 1%`
- `|error_E| <= 1%` (en ventana suficiente para reducir cuantizacion, por ejemplo >= 10 min).

## 10. Validacion con carga disponible (sin bombillo)

Si no hay bombillo, se puede usar pistola de silicona, pero considerar:

- Puede incluir control termico interno y transitorios.
- El `PF` puede subir cerca de `1.0` en tramos estables y caer durante conmutaciones.
- Para comparar precision, usar ventanas estables de al menos `60 s`.

Recomendacion practica:

- registrar 3 ventanas por nivel de carga (baja/media/alta temperatura de la pistola),
- usar promedio y desvio estandar de `P`, `I`, `PF`,
- descartar los primeros `5-10 s` despues de cada cambio de estado.

## 11. Criterios de aceptacion funcional

- deteccion AC sin oscilaciones espurias.
- `E_soft` monotona no decreciente cuando `P > umbral`.
- `E_nvs` monotona y persistente tras reboot.
- `E_hw`/`RAW_EHI` con tendencia coherente bajo carga (si `RAW_EHI` no cambia, abrir ticket de validacion HW energy path).

## 12. Formato recomendado de registro (CSV)

```csv
test_step,t_start_ms,t_end_ms,ac_state,load_desc,vrms_avg,irms_avg,p_avg,pf_avg,f_avg,e_soft_delta_wh,e_nvs_delta_wh,e_hw_delta_wh,raw_vrms_min,raw_vrms_max,raw_ehi_min,raw_ehi_max,notes
```

## 13. Salida esperada de Sprint 2

- reporte de pruebas con tabla de errores por carga,
- conclusion de cumplimiento/no cumplimiento `<=1%`,
- backlog tecnico para armonicos (frecuencia de muestreo, ventana FFT, THD objetivo).
