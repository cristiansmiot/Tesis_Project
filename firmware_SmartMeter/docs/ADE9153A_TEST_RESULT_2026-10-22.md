# Resultado de prueba aislada ADE9153A — 2026-10-22

## Objetivo

Verificar, antes de aplicar tension de red, que el ESP32-S3 reconoce el
ADE9153A, que la configuracion critica queda aplicada y que los registros de
metrologia pueden adquirirse de forma repetible.

Esta prueba valida integracion digital y adquisicion. No valida exactitud,
calibracion ni cumplimiento de clase metrologica.

## Montaje

- ESP32-S3 conectado por USB en COM6.
- EV-ADE9153ASHIELDZ alimentado y con jumper SPI en 3.3 V.
- SPI2: MOSI GPIO11, MISO GPIO13, SCLK GPIO12 y CS_ADE GPIO10.
- MicroSD completamente desconectada.
- LINE, NEUTRAL y CARGA desconectados.
- Perfil de firmware: `test_ade_measurement`.
- SPI: 1 MHz.

## Lectura de retorno de configuracion

| Registro | Leido | Esperado | Resultado |
| --- | ---: | ---: | --- |
| RUN | 0x0001 | 0x0001 | PASS |
| AI_PGAGAIN | 0x000A | 0x000A | PASS |
| BI_PGAGAIN | 0x0000 | 0x0000 | PASS |
| ACCMODE | 0x0010 | 0x0010 | PASS |
| CONFIG1 | 0x0300 | 0x0300 | PASS |
| CONFIG2 | 0x0C00 | 0x0C00 | PASS |
| CONFIG3 | 0x0000 | 0x0000 | PASS |
| COMPMODE | 0x0005 | 0x0005 | PASS |
| EP_CFG | 0x0009 | 0x0009 | PASS |
| EGY_TIME | 0x0F9F | 0x0F9F | PASS |
| TEMP_CFG estable | 0x0004 | 0x0004 | PASS |
| CONFIG0 | 0x00000000 | 0x00000000 | PASS |
| VLEVEL | 0x002C11E8 | 0x002C11E8 | PASS |
| VDIV_RSMALL | 0x000003E8 | 0x000003E8 | PASS |

Resultado: **14/14 PASS**. `TEMP_CFG` se escribe inicialmente como `0x000C`;
el bit de inicio se autolimpia y su valor estable es `0x0004`.

## Adquisicion sin entrada AC

Despues de 2 s de estabilizacion del DSP se adquirieron 30 grupos, con lectura
de AVRMS, AIRMS, AWATT, AVA, AFVAR, APF, APERIOD, energia activa y temperatura.

- Grupos leidos: 30/30.
- Errores SPI: 0.
- Reinicios durante la corrida: 0.
- `AC_VALID`: 0 en 30/30 muestras, coherente con entradas desconectadas.
- AVRMS estable: `0x0000032A`, aproximadamente 0.0109 V equivalentes.
- AIRMS estable: principalmente `0x00000273`, aproximadamente 0.000526 A
  equivalentes.
- AWATT, AVA, AFVAR y energia activa: cero estable.
- Temperatura interna observada: aproximadamente 24.5 C a 25.9 C.

Resultado final reportado por el equipo:

```text
HWTEST RESULT ADE MEAS ID PASS
HWTEST RESULT ADE CONFIG READBACK PASS - 14/14 registros coinciden
HWTEST RESULT ADE CONVERSION MODULES PASS
HWTEST RESULT ADE MEASUREMENT READS PASS - 30/30 grupos de registros leidos; AC no requerida
HWTEST RESULT ADE_MEASUREMENT PASS - configuracion y adquisicion digital aprobadas
```

## Interpretacion y limites

El ruido equivalente observado sirve como linea base para detectar cambios de
cableado o degradacion del enlace SPI. Con `AC_VALID=0`, los valores internos de
factor de potencia y frecuencia no representan una medicion fisica valida y no
deben incorporarse a estadisticas metrologicas.

La prueba permite avanzar a la fase energizada, pero no demuestra aun el error
porcentual de V, I, W o Wh. Esa evaluacion requiere un instrumento patron y un
montaje AC protegido.

## Siguiente secuencia autorizada

1. Tension nominal sin carga: validar `AC_VALID=1`, Vrms y frecuencia.
2. Carga resistiva pequena y conocida: validar polaridad, Irms, W, VA y PF.
3. Comparacion por puntos contra instrumento de referencia.
4. Energia acumulada durante un intervalo controlado.
5. mSure y calibracion manual solo despues de guardar la linea base y definir
   el patron de referencia.

