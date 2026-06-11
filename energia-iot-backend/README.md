# Backend — Plataforma de Monitoreo de Energía

API REST y consumidor MQTT del sistema de medición de energía residencial.
Trabajo de grado, Maestría en Ingeniería de IoT — Pontificia Universidad Javeriana.

El backend cumple dos roles que corren en el mismo proceso:

1. **API REST (FastAPI)** para el frontend: autenticación JWT con roles
   (super_admin / operador / visualizador), CRUD de dispositivos, consulta de
   mediciones y eventos, envío de comandos y auditoría de acciones.
2. **Cliente MQTT (paho-mqtt en hilo propio)** suscrito a `medidor/#`:
   decodifica SenML RFC 8428 de los nodos, persiste mediciones y salud del
   nodo, y genera eventos de calidad de tensión según CREG 024/2015
   (nominal 110 V ±10 % → 99–121 V, configurable por entorno con
   `VOLTAJE_NOMINAL` / `VOLTAJE_TOLERANCIA_PCT`).

## Topics MQTT que consume

| Topic | Contenido | Manejo |
|---|---|---|
| `medidor/{id}/datos` | Mediciones eléctricas (SenML) | Persiste en `mediciones`; si `bt` trae epoch absoluto (backlog SD del nodo) respeta esa hora |
| `medidor/{id}/estado` | Salud del nodo (SenML) | Actualiza `nodo_salud` y métricas de confiabilidad |
| `medidor/{id}/alerta` | Eventos de calidad de potencia | Crea registros en `eventos` |
| `medidor/{id}/conexion` | `online`/`offline` (LWT, retained) | Marca conectado/desconectado |

Los comandos viajan en sentido contrario por `medidor/{id}/cmd` como string
plano (`reset`, `relay_off`, `relay_on`, `sync_time`, `status`, `calibrate`)
— ver `routers/comandos.py`.

## Ejecución local

```cmd
python -m venv venv
venv\Scripts\activate
pip install -r requirements.txt
uvicorn app.main:app --reload
```

Swagger en http://localhost:8000/docs. Sin `DATABASE_URL` con `postgresql`
usa SQLite local (`energia_iot.db`); en Railway la variable se inyecta y
cambia a PostgreSQL automáticamente (ver `database.py`).

## Estructura

```
app/
├── config.py        # Settings Pydantic (BD, MQTT, umbrales CREG)
├── database.py      # Motor SQLAlchemy con auto-detección SQLite/PostgreSQL
├── main.py          # FastAPI + arranque del cliente MQTT
├── models/          # dispositivo, medicion, evento, nodo_salud, usuario, audit_log
├── schemas/         # Validación de entrada/salida
├── routers/         # auth, dispositivos, mediciones, eventos, comandos,
│                    # dashboard, salud, auditoria, admin
└── services/        # mqtt_client (consumidor SenML), auth (JWT), audit
```

## Despliegue

Railway (proyecto `energetic-curiosity`). `Procfile` y `railway.toml` en la
raíz. Producción: https://tesisproject-production.up.railway.app
