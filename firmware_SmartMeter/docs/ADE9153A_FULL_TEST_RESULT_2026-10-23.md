# Resultado de prueba integral ADE9153A e integracion sin microSD

Fecha: 2026-10-23  
Equipo bajo prueba: ESP32-S3 + EV-ADE9153ASHIELDZ + RTC + SIM7080G  
Puerto: COM6  
Condicion electrica: entrada AC desenergizada y sin carga durante la prueba
automatizada  
MicroSD: fisicamente desconectada

## Dictamen

El ADE9153A aprobo todas las funciones implementadas y comprobables de forma
segura sin aplicar carga. El firmware integrado tambien arranco y opero con
ADE, RTC, OLED, SIM7080G y MQTT, dejando el almacenamiento microSD
intencionalmente deshabilitado.

Este resultado demuestra integracion digital y funcional. No demuestra todavia
exactitud metrologica ni conformidad clase 1; esas conclusiones solo se pueden
obtener con tension y cargas controladas contra un instrumento patron.

## Resultado automatizado `test_ade_full`

| Bloque | Resultado | Evidencia funcional |
| --- | --- | --- |
| Identidad | PASS | RUN valido, VERSION `0x25`, PRODUCT_ID `0x0009153A` y CHIP_ID legible |
| Configuracion | PASS | Readback correcto de 14/14 registros criticos |
| Acceso SPI | PASS | Lecturas de 8/16/32 bits, escrituras idempotentes de 16/32 bits y contratos de argumentos |
| Registros | PASS | 27 registros de medicion, energia, calibracion, temperatura y mSure accesibles |
| Inicializacion de modulos | PASS | Voltaje, corriente, potencia, energia, frecuencia, temperatura, PQ y mSure inicializados |
| Conversiones | PASS | Vrms, frecuencia, Irms A/B, P, Q, S, PF, Wh, VARh, VAh y temperatura finitos |
| Interfaz mSure | PASS | Inicializacion, estado y validadores; sin modificar ganancias reales |
| Calidad de energia | PASS | Sag, swell, frecuencia fuera de rango y sobretemperatura activan y limpian la bandera esperada |
| Armonicos | PASS condicionado | El firmware devuelve de forma determinista `ESP_ERR_NOT_SUPPORTED`, comportamiento esperado del stub actual |
| Recuperacion | PASS | PROBE, SOFT_INIT y SPI_REINIT restablecen identidad y configuracion |
| Posrecuperacion | PASS | Temperatura y conversiones disponibles despues de recuperar |
| Resultado global | **PASS** | `HWTEST RESULT ADE_FULL PASS` |

Como linea base sin red AC, se observaron valores internos equivalentes
aproximados de 0,51 V, 0,00053 a 0,00061 A y 26,8 a 28,2 grados C. Son ruido y
offset de entrada sin calibrar, no lecturas metrologicas. El firmware integrado
los enmascara correctamente y entrega cero cuando el estado es `AC=LOST`.

## Hallazgo corregido durante la prueba

La recuperacion original exigia que VERSION fuera valido inmediatamente despues
del reset. En esta placa VERSION queda temporalmente en cero antes de ejecutar
RUN. Se corrigio la secuencia a:

1. reset por `CONFIG1.SWRST`;
2. escritura y verificacion de configuracion;
3. escritura de RUN;
4. verificacion de VERSION, PRODUCT_ID y CHIP_ID.

Con esa secuencia, SOFT_INIT y SPI_REINIT aprobaron. El reset fisico permanece
en el arranque inicial; las recuperaciones en ejecucion usan SWRST y no dependen
de modificar el cableado de la PCB.

## Integracion `integration_no_sd`

| Subsistema | Resultado observado |
| --- | --- |
| ADE9153A | `ADE_OK=1`, temperatura valida, medicion cada segundo |
| Deteccion AC | `AC=LOST` y magnitudes publicadas en cero, correcto al estar desenergizado |
| RTC | Recuperado en I2C `0x68`; sincronizacion remota aceptada y estado saludable |
| SIM7080G | AT recuperado automaticamente, registro LTE CAT-M1, contexto PDP disponible |
| MQTT | Conexion, comando `sync_time`, alertas, `/estado` y `/datos` en vivo operativos |
| OLED/UI | Inicializada y ejecutando la interfaz |
| microSD | `DISABLED` por configuracion, sin recuperaciones espurias ni bloqueo del sistema |

Se observo un reinicio no controlado en la fase temprana de conexion del modem.
No fue posible recuperar retrospectivamente la causa de reset. Despues de la
recuperacion automatica, el `uptime` avanzo de forma monotona hasta 225 s sin un
segundo reinicio. Esto permite aceptar el arranque funcional, pero no declarar
aun estabilidad continua.

## Funciones no concluidas y razon

- Pico de tension: `voltage_get_vpeak()` sigue siendo un stub.
- Armonicos: modulo declarado pero no implementado.
- Canal de corriente B: deshabilitado por configuracion del producto.
- mSure real: no se ejecuto porque modifica `AIGAIN` y `AVGAIN`; requiere
  respaldo de coeficientes, linea base y protocolo de calibracion.
- Exactitud y energia: pendientes de tension/carga controlada e instrumento
  patron.
- Consumo de baterias: el ADE mide la ruta AC externa; no puede detectar el
  consumo DC propio del sistema.
- Auditoria local y reenvio: pendientes de resolver el hardware microSD.

## Criterio para continuar

1. Mantener la microSD desconectada.
2. Ejecutar estabilidad sin intervenir el montaje: 30 min y luego 2 h.
3. Registrar razon de reset al inicio y verificar cero reinicios adicionales.
4. Solo despues, realizar prueba AC sin carga y verificar aproximadamente 120 V
   y 60 Hz con el montaje protegido.
5. Ejecutar calibracion y puntos de exactitud contra el instrumento patron.
6. Integrar la microSD como ultimo bloque, sin compartir un MISO conducido
   permanentemente por dos perifericos.
