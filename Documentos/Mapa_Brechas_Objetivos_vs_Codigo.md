# Mapa de brechas: objetivos de tesis vs. estado del código

**Fuentes:** AnteProyecto_V3.pdf y MIICO Plan de Trabajo I-II v2 (06-02-2026).
**Fecha de análisis:** 2026-06-10.
**Componentes evaluados:** firmware_SmartMeter, energia-iot-backend, energia-iot-frontend, mosquitto-config.

Nota de contexto: el plan MIICO reporta avances de febrero 2026 (35/25/25/0 %). El código
avanzó mucho desde entonces; este documento refleja el estado real a junio 2026.

## Objetivo 1 — Medidor según NTC 6079 (voltaje y corriente residencial)

| Compromiso | Estado en código | Brecha |
|---|---|---|
| Firmware de medición con ADE9153A por SPI | Hecho: `drivers/ade9153a/` + `modules/measurements/` (V, I, P, Q, S, FP) con tarea FreeRTOS a 1 s | — |
| Calibración del dispositivo (mSure) | Parcial: `modules/calibration/` con persistencia NVS | Falta protocolo de calibración documentado contra patrón |
| PCB, ensamblaje | En curso: `PCB_Project_Meter/` (sin trackear en git) | Commitear diseño y documentar fabricación |
| Pruebas de precisión vs. medidor certificado | No iniciado | Banco de pruebas + informe comparativo (entregable Abr 27) |
| Calidad de potencia (sag/swell, armónicos) | Esqueleto: `modules/power_quality/` con stubs | Implementar detección real con registros del ADE9153A |

## Objetivo 2 — Comunicación medidor → Internet

| Compromiso | Estado en código | Brecha |
|---|---|---|
| Módulo celular + MQTT | Hecho: SIM7080G (reemplazo justificado del SIM7020 del anteproyecto) con MQTT, SenML RFC 8428, reconexión rápida (caché de baud en NVS) | Documentar en informe el cambio SIM7020→SIM7080G y su justificación (LTE-M/NB-IoT dual) |
| Comunicación segura | Parcial: auth de broker con ACL en mosquitto-config | TLS en el enlace MQTT (el anteproyecto compromete ciberseguridad transversal) |
| Gestión de errores del módulo | Hecho: recuperación de deadlock PDP, métricas de confiabilidad, reconciliación de energía | — |
| Buffer ante pérdida de enlace | Parcial: MicroSD inicializada (SPI3) pero sin lógica de buffer | Implementar store-and-forward en SD cuando MQTT caiga |
| Pruebas de campo (alcance, tasa, confiabilidad) | Protocolo P8 de 24 h redactado | Ejecutar y documentar resultados |

## Objetivo 3 — Plataforma web (visualización, estado de nodos, control)

| Compromiso | Estado en código | Brecha |
|---|---|---|
| Visualización V/I RMS, consumo | Hecho: dashboard + detalle de medidor con Recharts | Unidades en ejes (pendiente del director) |
| Estado de nodos | Hecho: `nodo_salud` en backend + panel de estado | Indicador de tensión en dashboard (pendiente del director) |
| Control de nodos | Parcial: backend tiene routers/comandos.py y flujo MQTT verificado | Tab "Comandos" en el frontend (pendiente del director) |
| Alertas y notificaciones | Parcial: eventos y gateo por presencia de AC | Alertas por umbral CREG 024/2015: 110 V ±10 % → 99–121 V |
| Gestión de usuarios y perfiles | Hecho: JWT + RBAC (super_admin, operador, visualizador) + auditoría | — |
| Multi-nodo (escalar de 1 a N) | Hecho a nivel de datos: UID por IMEI, modelo Dispositivo | Verificar que dashboard agregue N dispositivos sin supuestos de nodo único |

Nota: el anteproyecto proponía PHP/MySQL/cPanel; la implementación real es
FastAPI/PostgreSQL/Railway. Justificar la decisión en el informe final (mejor
soporte async para MQTT, tipado con Pydantic, despliegue PaaS).

## Objetivo 4 — Evaluación del sistema (exactitud y desempeño)

| Compromiso | Estado en código | Brecha |
|---|---|---|
| Exactitud de parámetros eléctricos | No iniciado | Diseñar banco de pruebas (multímetro de precisión o banco certificado) |
| Consumo energético del nodo | No iniciado | Medir consumo en operación y en reconexión |
| Alcance/cobertura celular | No iniciado | Pruebas de campo con métricas de señal del SIM7080G (CSQ/RSRP ya disponibles por AT) |
| Pruebas funcionales/rendimiento/usabilidad de la plataforma | Parcial: tests/ en backend | Plan de pruebas formal + resultados |

## Lectura contra el plan de entregables MIICO

- Entregables de feb–abr (investigación, firmware base, comunicaciones, plataforma): **cubiertos por el código actual**, falta consolidar la documentación de cada uno.
- Entregable Abr 27 (sistema validado: seguridad, calibración, comparativas): **es la brecha principal** — TLS, protocolo de calibración y banco de pruebas.
- Entregable May 15 (sistema completo con alertas): depende de los pendientes de frontend (CREG, comandos) y del cierre de hardware.
- Entrega final May 29: informe + demostración.

## Prioridades derivadas (orden sugerido)

1. Pendientes de frontend del director (visibles en la demo, esfuerzo bajo).
2. Robustez del firmware: degradación por módulo, watchdog por tarea, buffer SD (sustenta el objetivo 2 y la prueba de 24 h).
3. TLS en MQTT (compromiso explícito de ciberseguridad del anteproyecto).
4. Objetivo 4 completo: plan de pruebas de exactitud, consumo y cobertura — es el objetivo con 0 % y el que valida la tesis.
