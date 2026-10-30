# Protocolo integral de funciones ADE9153A — 2026-10-23

## Objetivo

Comprobar en la placa real todas las funciones del ADE9153A que hoy estan
implementadas en el firmware, identificar explicitamente las funciones que son
stubs o estan deshabilitadas y, despues, arrancar el firmware completo con la
microSD omitida por configuracion.

Esta prueba valida integracion digital, conversiones, diagnostico y
recuperacion. No demuestra exactitud metrologica ni cumplimiento de clase 1;
eso requiere comparar contra el instrumento patron en varios puntos de carga.

## Condiciones de seguridad

- La microSD debe permanecer fisicamente desconectada.
- No cargar firmware ni manipular la PCB con LINE/NEUTRAL energizados.
- Ejecutar inicialmente sin carga conectada.
- Mantener proteccion AC, puesta a tierra del montaje e instrumentos adecuados.
- El jumper SPI del EV-ADE9153ASHIELDZ debe permanecer en 3.3 V.

## Perfil y comandos

Perfil exhaustivo:

```powershell
pio run -e test_ade_full -t upload --upload-port COM6
pio device monitor -p COM6 -b 115200
```

Firmware integrado posterior, con almacenamiento deshabilitado de forma
intencional:

```powershell
pio run -e integration_no_sd -t upload --upload-port COM6
pio device monitor -p COM6 -b 115200
```

## Cobertura automatizada del perfil `test_ade_full`

| Bloque | Funciones o comportamiento | Criterio |
| --- | --- | --- |
| Identidad | `ade9153a_init`, `ade9153a_get_diag`, `ade9153a_is_ready` | RUN, VERSION, PRODUCT y CHIP_ID validos |
| SPI | lectura 8/16/32 bits, escritura 16/32 bits y argumentos nulos | lectura coherente; escrituras idempotentes; errores de contrato correctos |
| Configuracion | PGA, 60 Hz, VLEVEL y 14 registros criticos | readback 14/14 |
| Registros | 27 registros disponibles de medicion, energia, ganancia, temperatura y mSure | todas las transacciones retornan `ESP_OK` |
| Conversiones | Vrms, frecuencia, Irms A/B, P, Q, S, PF, Wh, VARh, VAh y temperatura | valores finitos y dentro de limites de sanidad |
| Calidad de energia | sag, swell, frecuencia fuera de rango y sobretemperatura | cada estimulo sintetico activa y limpia su bandera correcta |
| mSure seguro | inicializacion, registro de estado, contratos y validadores | interfaz legible sin ejecutar autocalibracion |
| Recuperacion | PROBE, SOFT_INIT y SPI_REINIT | identidad y configuracion validas despues de cada accion |
| Posrecuperacion | conversiones despues de reinicializar SPI y DSP | mediciones disponibles nuevamente |

Resultado esperado:

```text
HWTEST RESULT ADE FULL ID PASS
HWTEST RESULT ADE FULL CONFIG PASS
HWTEST RESULT ADE FULL ACCESS PASS
HWTEST RESULT ADE FULL REGISTERS PASS
HWTEST RESULT ADE FULL MODULE INIT PASS
HWTEST RESULT ADE FULL CONVERSIONS PASS
HWTEST RESULT ADE FULL MSURE INTERFACE PASS
HWTEST RESULT ADE FULL POWER QUALITY PASS
HWTEST RESULT ADE FULL HARMONICS PASS - NOT_IMPLEMENTED esperado
HWTEST RESULT ADE FULL RECOVERY PASS
HWTEST RESULT ADE FULL POST-RECOVERY PASS
HWTEST RESULT ADE_FULL PASS
```

## Exclusiones deliberadas

- `voltage_get_vpeak()` es actualmente un stub. Se invoca y se registra como
  `NOT_IMPLEMENTED`, pero no se interpreta como una medicion valida.
- `harmonics_init()` retorna `ESP_ERR_NOT_SUPPORTED`. El perfil comprueba que
  esa condicion conocida se reporte de manera determinista.
- El canal de corriente B esta deshabilitado porque `METER_BI_PGAGAIN=1`; se
  registra como `DISABLED_BY_CONFIG`.
- La ejecucion real de mSure se difiere. mSure escribe `AIGAIN` y `AVGAIN`; se
  ejecutara solamente dentro del protocolo de calibracion, con linea base,
  patron de referencia y respaldo de los coeficientes.
- La deteccion de corriente de la bateria o del consumo interno no corresponde
  al ADE: el shield mide la ruta AC externa, no la alimentacion DC del sistema.

## Integracion sin microSD

El perfil `integration_no_sd` conserva ADE, RTC, OLED, SIM7080G, red celular,
MQTT, mediciones, calidad de energia, NVS y comandos. No inicia el servicio de
almacenamiento, marca SD como `DISABLED` y publica telemetria en vivo. Mientras
la SD no este resuelta no existe auditoria local duradera ni reenvio de datos
despues de una caida de red; esa limitacion debe quedar visible en el informe.

La integracion se acepta inicialmente si:

1. ADE, RTC/TIME, CEL y MQTT llegan a `OK` sin reinicios.
2. SD aparece deshabilitada, no como falla que cause recuperaciones espurias.
3. `/datos` cambia cada periodo e incluye mediciones y trazabilidad.
4. `/estado` publica salud, contadores y metricas de radio disponibles.
5. La plataforma mantiene hora valida y actualiza la ultima lectura.

## Estado de preparacion

- `test_ade_full`: compilacion y prueba en placa PASS.
- `integration_no_sd`: compilacion, carga y arranque en placa PASS.
- ADE: `ADE_OK=1`, lectura de temperatura valida y adquisicion continua.
- RTC: recuperado y sincronizado mediante comando remoto; estado saludable.
- SIM7080G: recuperacion automatica por PWRKEY, registro LTE CAT-M1 y PDP PASS.
- MQTT: conexion, recepcion de comando, publicacion de alertas, `/estado` y
  telemetria `/datos` en vivo PASS.
- SD: deshabilitada de forma deliberada; no se inicia ni se reporta como falla.

Durante el arranque integrado se observo un reinicio no controlado temprano. El
sistema se recupero automaticamente y la observacion posterior mantuvo el
`uptime` monotono entre 57 s y 225 s, sin otro reinicio. Por tanto, la
integracion funcional queda aceptada, pero la estabilidad prolongada debe
confirmarse con una corrida posterior de 30 minutos, 2 horas y finalmente 24
horas. El resultado detallado se conserva en
`ADE9153A_FULL_TEST_RESULT_2026-10-23.md`.
