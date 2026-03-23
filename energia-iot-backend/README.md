# Backend - API Medidor de Energía IoT

FastAPI + PostgreSQL + MQTT para el sistema de medición de energía eléctrica.
Tesis de Maestría en IoT — Pontificia Universidad Javeriana.

---

## Acceso en producción (Railway)

El backend está desplegado en Railway y se conecta automáticamente al broker MQTT.

### Encontrar la URL de tu servicio

1. Ir a [railway.app](https://railway.app) → tu proyecto
2. Hacer clic en el servicio **energia-iot-backend**
3. Pestaña **Settings** → sección **Domains** → copiar la URL pública

La URL tiene el formato:
```
https://energia-iot-backend-production-XXXX.up.railway.app
```

### Verificar que el backend está vivo

```bash
# Health check (debe responder {"status": "healthy"})
curl https://TU-URL-BACKEND.up.railway.app/health

# Raíz (confirma version y estado)
curl https://TU-URL-BACKEND.up.railway.app/

# Estado de la conexión MQTT
curl https://TU-URL-BACKEND.up.railway.app/api/v1/mqtt/status
```

O desde el navegador: `https://TU-URL-BACKEND.up.railway.app/docs`
Esa URL abre el **Swagger UI** con todos los endpoints interactivos.

---

## API — Referencia de endpoints

Prefijo base: `/api/v1`

### Dispositivos

| Método | Ruta | Descripción |
|--------|------|-------------|
| `GET` | `/dispositivos/` | Listar todos los dispositivos |
| `GET` | `/dispositivos/{device_id}` | Obtener un dispositivo por ID |
| `POST` | `/dispositivos/` | Crear dispositivo manualmente |
| `PATCH` | `/dispositivos/{device_id}` | Actualizar nombre/ubicación |
| `DELETE` | `/dispositivos/{device_id}` | Eliminar dispositivo |
| `GET` | `/dispositivos/{device_id}/estado` | Estado de conexión actual |
| `GET` | `/dispositivos/{device_id}/salud` | Último registro de salud del nodo |

El `device_id` del ESP32 es: **`MEDIDOR_CRISTIAN_001`**

Ejemplo para el medidor desplegado:
```bash
curl https://TU-URL-BACKEND.up.railway.app/api/v1/dispositivos/MEDIDOR_CRISTIAN_001/estado
```

### Mediciones eléctricas

| Método | Ruta | Descripción |
|--------|------|-------------|
| `GET` | `/mediciones/` | Listar últimas mediciones (param: `device_id`, `limit`) |
| `GET` | `/mediciones/{device_id}/ultima` | Última medición del dispositivo |
| `GET` | `/mediciones/{device_id}/resumen` | Resumen formateado (V, A, W, kWh) |
| `GET` | `/mediciones/{device_id}/historico?horas=24` | Histórico + estadísticas |
| `POST` | `/mediciones/` | Crear medición manual (pruebas) |

Ejemplo — última medición del ESP32:
```bash
curl https://TU-URL-BACKEND.up.railway.app/api/v1/mediciones/MEDIDOR_CRISTIAN_001/ultima
```

Ejemplo — histórico últimas 6 horas:
```bash
curl "https://TU-URL-BACKEND.up.railway.app/api/v1/mediciones/MEDIDOR_CRISTIAN_001/historico?horas=6"
```

### Salud del nodo (métricas de transmisión)

| Método | Ruta | Descripción |
|--------|------|-------------|
| `GET` | `/salud/{device_id}/metricas?horas=24` | PDR, RSSI histórico, reconexiones |
| `GET` | `/salud/{device_id}/historico?horas=24` | Registros completos de salud |

Ejemplo — métricas de las últimas 24h:
```bash
curl "https://TU-URL-BACKEND.up.railway.app/api/v1/salud/MEDIDOR_CRISTIAN_001/metricas?horas=24"
```

### Endpoints de sistema

| Método | Ruta | Descripción |
|--------|------|-------------|
| `GET` | `/` | Info de la app y links |
| `GET` | `/health` | Health check (Railway lo usa para monitorear) |
| `GET` | `/api/v1/mqtt/status` | Estado del cliente MQTT interno |
| `GET` | `/docs` | Swagger UI interactivo |
| `GET` | `/redoc` | Documentación ReDoc |

---

## Cómo fluyen los datos

```
ESP32 (firmware)
    → publica MQTT en tópico medidor/ESP32-001/datos  (cada 60s)
    → publica MQTT en tópico medidor/ESP32-001/estado (cada 5min)
    → publica MQTT en tópico medidor/ESP32-001/conexion (al conectar)

Broker MQTT (Railway Mosquitto)  shinkansen.proxy.rlwy.net:58954
    → reenvía mensajes al cliente MQTT del backend

Backend (FastAPI en Railway)
    → recibe mensajes MQTT y los guarda en PostgreSQL
    → expone los datos vía REST API

Frontend (React en Railway)
    → consulta la API REST cada 30s y muestra el dashboard
```

---

## Ejecutar localmente (desarrollo)

### Requisitos
- Python 3.11+
- (Opcional) PostgreSQL local — por defecto usa SQLite

### Pasos

```cmd
cd energia-iot-backend
python -m venv venv
venv\Scripts\activate
pip install -r requirements.txt
```

Crear archivo `.env` (copiar desde `.env.example`):
```env
USE_SQLITE=true
ENVIRONMENT=development
DEBUG=true
MQTT_BROKER=shinkansen.proxy.rlwy.net
MQTT_PORT=58954
MQTT_USERNAME=medidor_iot
MQTT_PASSWORD=Colombia2026$
MQTT_TOPIC_BASE=medidor/#
MQTT_USE_TLS=true
```

Arrancar el servidor:
```cmd
uvicorn app.main:app --reload
```

Acceder a: **http://localhost:8000/docs**

---

## Variables de entorno en Railway

Configurar en: Railway → servicio backend → pestaña **Variables**

| Variable | Valor en producción |
|----------|-------------------|
| `USE_SQLITE` | `false` |
| `DATABASE_URL` | (Railway la inyecta automáticamente desde PostgreSQL) |
| `ENVIRONMENT` | `production` |
| `DEBUG` | `false` |
| `MQTT_BROKER` | `shinkansen.proxy.rlwy.net` |
| `MQTT_PORT` | `58954` |
| `MQTT_USERNAME` | `medidor_iot` |
| `MQTT_PASSWORD` | `Colombia2026$` |
| `MQTT_TOPIC_BASE` | `medidor/#` |
| `MQTT_USE_TLS` | `true` |
| `CORS_ORIGINS` | URL del frontend Railway (ej: `https://frontend.up.railway.app`) |

---

## Estructura del proyecto

```
energia-iot-backend/
├── app/
│   ├── config.py          # Variables de entorno (Pydantic Settings)
│   ├── database.py        # Conexión SQLAlchemy / SQLite fallback
│   ├── main.py            # FastAPI app + lifespan (MQTT + BD)
│   ├── models/
│   │   ├── dispositivo.py # Tabla dispositivos
│   │   ├── medicion.py    # Tabla mediciones eléctricas
│   │   └── nodo_salud.py  # Tabla salud del nodo (topic /estado)
│   ├── routers/
│   │   ├── dispositivos.py # CRUD dispositivos
│   │   ├── mediciones.py   # CRUD + consultas históricas
│   │   └── salud.py        # Métricas PDR, RSSI, reconexiones
│   ├── schemas/           # Pydantic — validación request/response
│   └── services/
│       └── mqtt_client.py # Suscriptor MQTT → guarda en BD
├── tests/
├── .env.example
├── railway.toml           # Config de despliegue Railway
├── requirements.txt
└── Procfile
```
