# Resumen del estado actual del Smart Meter

Fecha de corte: **24 de octubre de 2026**

Nodo: `SM-348518435344`

Firmware de caracterizacion: `1.1.0-obj4`

Plataforma desplegada: commit `9fca7d1` en Railway

## 1. Dictamen ejecutivo

El prototipo ya supero la integracion funcional de ADE9153A, RTC, OLED,
teclado, SIM7080G/CAT-M1, MQTT y plataforma. La corrida basal de 12 horas con
AC y sin carga cumplio los criterios de estabilidad: no hubo reinicios, caidas
de AC ni perdidas del ADE; llegaron 713 secuencias consecutivas sin huecos,
duplicados ni conflictos. Se observaron dos incidentes MQTT recuperados, sin
perdida de secuencia.

La microSD comparte SPI2 con el ADE mediante CS independientes (`SD=GPIO7`,
`ADE=GPIO10`). Se elimino la doble inicializacion del bus y se redujo el reloj
de la SD de 20 MHz a 4 MHz. Despues del cambio, ADE y SD operaron juntos en la
prueba de humo. Falta demostrar esa convivencia durante una corrida prolongada
y conciliar la auditoria SD con la base de datos.

El sistema esta listo para pasar de validacion funcional a caracterizacion
metrologica. No se debe afirmar todavia que es clase 1: faltan las pruebas con
instrumento de referencia, la incertidumbre y la comparacion antes/despues de
calibrar.

## 2. Estado por subsistema

| Subsistema | Estado al corte | Evidencia disponible | Pendiente de cierre |
|---|---|---|---|
| ADE9153A digital | **PASS** | Identidad, RUN, SPI 30/30, temperatura y recuperacion funcional | Exactitud e incertidumbre con referencia |
| Medicion sin carga | **PASS preliminar** | 12 h: 0 A, 0 W y 0 Wh en 713 muestras | Repetir con SD activa y referencia conectada |
| RTC/tiempo | **PASS funcional** | Retencion aproximada de 24 h; `RTC:OK`, `TIME:OK`, fuente RTC; 3.290 a 3.267 V | Deriva cuantitativa y cierre electrico de la ruta de respaldo |
| SIM7080G/CAT-M1 | **PASS** | Registro, PDP, MQTT, CPSI y recuperacion automatica | Ensayos controlados de perdida de cobertura/PDP/broker |
| MQTT/backend | **PASS basal** | 713/713 secuencias recibidas; latencia p95 15.432 s | ACK extremo a extremo mediante conciliacion SD-BD |
| microSD | **PASS de humo** | Bus compartido centralizado; SD a 4 MHz; escritura y drenado observados | Corrida prolongada, corte de red y reconciliacion |
| OLED/teclado | **PASS funcional** | Cuatro teclas detectadas y mapa cargado | Repetir una secuencia corta tras el remapeo definitivo |
| Plataforma | **DESPLEGADA** | Backend, frontend, PostgreSQL y broker online en Railway; commit `9fca7d1` | Prueba de aceptacion visual y exportacion de evidencia |
| Alarmas | **IMPLEMENTADAS** | Eventos `raised/cleared`, activos separados del historico; banda residencial 108-132 V sostenida | Inyeccion controlada de cada estado y medicion del tiempo de deteccion |
| Calibracion | **PENDIENTE** | Ruta mSure/manual y persistencia NVS implementadas | Ensayo comparativo con patron; `CAL=0x00` no es cierre |

## 3. Resultado cuantitativo de la corrida de 12 horas

Ventana: `2026-10-23 21:38:50` a `2026-10-24 09:38:50`, hora de Bogota.

Configuracion: AC energizada, sin carga externa y microSD desconectada.

| Indicador | Resultado |
|---|---:|
| Reinicios no controlados | 0 |
| Disponibilidad AC / ADE | 100 % / 100 % |
| Mediciones | 713, `seq=1..713` |
| PDR de mensajes generados | 100 % |
| Cobertura temporal nominal | 713/720 = 99.03 % |
| Huecos / duplicados / conflictos | 0 / 0 / 0 |
| Intervalo mediano / maximo | 60 s / 127 s |
| Hora valida | 713/713 |
| Latencia p50 / p95 / p99 | 14.937 / 15.432 / 21.797 s |
| Latencia maxima | 47.411 s |
| Vrms min / promedio / max | 120.073 / 120.160 / 120.280 V |
| Frecuencia min / promedio / max | 59.927 / 59.999 / 60.085 Hz |
| Energia inicial / final | 0 / 0 Wh |
| Incidentes MQTT | 2, ambos recuperados; 0 activos al cierre |
| Reconexiones contabilizadas | 1 |

El PDR de 100 % significa que llegaron todas las muestras que el firmware
genero, no que se generaron exactamente 720 lecturas. La pausa maxima de 127 s
explica las siete muestras nominales faltantes y refuerza la necesidad de usar
la SD como fuente de verdad en la prueba oficial.

## 4. Estado de los bloqueadores del Objetivo 4

| ID | Estado | Evidencia / condicion restante |
|---|---|---|
| B06 Trazabilidad | **CERRADO funcionalmente** | `boot_id`, `seq`, `ts_device`, `uptime_ms`, `fw` y hora de servidor se observaron durante 12 h |
| B07 Radio | **CERRADO funcionalmente** | RSRP/RSRQ/SINR persistidos; falta ampliar la muestra bajo fallas de red |
| B08 Contadores | **CERRADO funcionalmente** | Generacion, intentos, aceptaciones, sesiones y reconexiones separados |
| B09 Copia local comparable | **IMPLEMENTADO; cierre experimental pendiente** | Requiere reconciliar auditoria SD-BD-dashboard y comprobar drenado del backlog |
| B04 Referencia | **DISPONIBLE, por identificar** | Fluke 43B prestado: registrar serie, sonda, rango, especificacion y calibracion |
| B05 Calibracion | **PENDIENTE** | Comparacion antes/despues y persistencia tras reinicio |
| B10 Piloto completo | **PARCIAL** | 12 h aprobadas sin SD; falta 24 h con todos los modulos |

## 5. Plataforma y criterio de tension

La plataforma desplegada usa 120 V nominales para el escenario residencial.
La banda operativa configurada es 108 a 132 V (90 % a 110 %) y una excursion
de una sola muestra no crea por si sola una alarma regulatoria: la condicion
debe mantenerse al menos 60 s y confirmarse con lecturas consecutivas. Los
eventos resueltos quedan como historico con inicio y fin; solo las condiciones
vigentes se muestran como alarmas activas.

Esto es una regla operativa de la plataforma, no una certificacion de calidad
de energia. La defensa debe citar la fuente regulatoria y mostrar el tiempo de
persistencia aplicado.

## 6. Pruebas siguientes, en orden

1. **G1 - Integracion completa de 60 minutos.** ADE, SD, RTC, OLED, teclado y
   SIM conectados; AC con proteccion y sin carga. Verificar cero errores SPI
   persistentes, una muestra/minuto, archivos SD legibles, `seq` consecutivo y
   funcionamiento de UP/DOWN/SELECT/BACK.
2. **P07 - Conciliacion SD-BD-dashboard.** Comparar por
   `device_id+boot_id+seq` al menos 30 registros (10 inicio, 10 centro, 10 fin).
   Criterio: 100 % de correspondencia y cero conflictos.
3. **P06 - Corte controlado de comunicaciones.** Interrumpir red o broker 10
   minutos, comprobar que la SD sigue registrando y medir deteccion,
   reconexion, RTO y tiempo de drenado. Criterio: perdida neta cero despues del
   drenado.
4. **P01 - Exactitud estatica con Fluke 43B.** Registrar primero el estado de
   calibracion del instrumento y de la pinza. Medir V, I, P, FP y frecuencia en
   cargas resistiva, inductiva y no lineal, con 30 observaciones por punto.
5. **P02/P04 - Energia y calibracion.** Determinar si el 43B permite una
   referencia de energia adecuada para el objetivo. Si no, usarlo para V/I/P/FP
   y conseguir un patron de Wh. Comparar exactamente los mismos puntos antes y
   despues de mSure/calibracion manual.
6. **P05 - Corrida oficial de 24 horas.** Todos los modulos activos y SD como
   fuente de verdad. Criterios: 0 reinicios, PDR en vivo >=99 %, completitud
   eventual 100 %, tiempo valido 100 % y alarmas explicadas.
7. **P08 - Offline/online y alarmas.** Medir el tiempo hasta Offline, el retorno
   a Online y cada transicion ADE/comunicaciones; conservar eventos despejados.

No conviene iniciar la corrida oficial de 24 h hasta que G1, la conciliacion de
30 registros y un corte de red corto hayan aprobado. Asi se evita perder un dia
por un problema que puede detectarse en una hora.

## 7. Archivos de evidencia principales

- `docs/PROTOCOL_12H_ENERGIZED_NO_LOAD_2026-10-23.md`
- `docs/ADE9153A_TEST_RESULT_2026-10-22.md`
- `docs/ADE9153A_FULL_TEST_RESULT_2026-10-23.md`
- `docs/OBJ4_TELEMETRY_VALIDATION.md`
- `docs/HARDWARE_TEST_RESULTS.md`
- `../../Documentos/Protocolo_Caracterizacion_Objetivo4_v0.2.md`

## 8. Mensaje defendible al corte

El prototipo ha demostrado integracion funcional, trazabilidad de datos y
recuperacion automatica en una corrida basal de 12 horas. La etapa pendiente no
es demostrar que los modulos encienden, sino cuantificar exactitud,
incertidumbre, integridad extremo a extremo y recuperacion bajo fallas. Esa
separacion evita confundir validacion funcional con certificacion metrologica.
