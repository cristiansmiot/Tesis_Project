# Sistema IoT - Medidor de Energía Eléctrica

Plataforma completa de medición de energía eléctrica residencial.
Tesis de Maestría en IoT — Pontificia Universidad Javeriana.

---

## Arquitectura del sistema

```
┌─────────────────────────────────────────────────────────────────┐
│                        HARDWARE (ESP32-S3)                      │
│  ADE9153A ──SPI──► ESP32-S3 ──UART──► SIM7080G ──NB-IoT──────┐ │
│  (metrología)       (firmware)         (modem)                 │ │
└────────────────────────────────────────────────────────────────┼─┘
                                                                 │
                                              MQTT QoS0/1        │
                                              Tópicos:           │
                                              medidor/ESP32-001/ │
                                              ├─ datos           │
                                              ├─ estado          │
                                              ├─ alerta          │
                                              └─ conexion        │
                                                                 ▼
┌─────────────────────────────────────────────────────────────────┐
│              RAILWAY (cloud)                                    │
│                                                                 │
│  ┌──────────────┐    MQTT     ┌─────────────────┐             │
│  │   Mosquitto  │◄────────────│  Backend FastAPI │             │
│  │   (broker)   │────────────►│  + PostgreSQL    │             │
│  └──────────────┘             └────────┬────────┘             │
│   Host: shinkansen.proxy.rlwy.net      │  REST API /api/v1    │
│   Puerto: 58954                        ▼                       │
│                               ┌─────────────────┐             │
│                               │ Frontend React  │             │
│                               │  (dashboard)    │             │
│                               └─────────────────┘             │
└─────────────────────────────────────────────────────────────────┘
```

---

## URLs en producción (Railway)

Las URLs exactas se encuentran en el [panel de Railway](https://railway.app) → tu proyecto → cada servicio → Settings → Domains.

### Cómo encontrarlas paso a paso

1. Entrar a [railway.app](https://railway.app) con tu cuenta
2. Abrir el proyecto del medidor
3. Para el **backend**: clic en servicio `energia-iot-backend` → Settings → Domains
4. Para el **frontend**: clic en servicio `energia-iot-frontend` → Settings → Domains

### Verificación rápida

Una vez que tienes las URLs, verifica cada componente:

```bash
# 1. Backend — debe responder {"status": "healthy"}
curl https://TU-BACKEND.up.railway.app/health

# 2. Swagger UI — abre en el navegador, lista todos los endpoints
# https://TU-BACKEND.up.railway.app/docs

# 3. Estado del cliente MQTT interno del backend
curl https://TU-BACKEND.up.railway.app/api/v1/mqtt/status

# 4. Frontend — abrir en el navegador directamente
# https://TU-FRONTEND.up.railway.app
```

### Verificar que llegan datos del ESP32

```bash
# Última medición eléctrica recibida
curl https://TU-BACKEND.up.railway.app/api/v1/mediciones/MEDIDOR_CRISTIAN_001/ultima

# Estado de conexión del dispositivo
curl https://TU-BACKEND.up.railway.app/api/v1/dispositivos/MEDIDOR_CRISTIAN_001/estado

# Salud del nodo (RSSI, reconexiones, PDR)
curl https://TU-BACKEND.up.railway.app/api/v1/salud/MEDIDOR_CRISTIAN_001/metricas
```

---

## Repositorios / carpetas

| Carpeta | Descripción | README |
|---------|-------------|--------|
| `energia-iot-backend/` | API FastAPI + suscriptor MQTT + PostgreSQL | [README](energia-iot-backend/README.md) |
| `energia-iot-frontend/` | Dashboard React en tiempo real | [README](energia-iot-frontend/README.md) |
| `mosquitto-config/` | Configuración del broker MQTT en Railway | — |

El firmware del ESP32 vive en un repositorio separado: `firmware_SmartMeter`.

---

## Datos que publica el firmware

El ESP32 publica automáticamente en MQTT cada vez que está conectado:

| Tópico | Intervalo | Contenido |
|--------|-----------|-----------|
| `medidor/ESP32-001/datos` | 60 s | Mediciones eléctricas (V, A, W, Wh, FP, Hz) en formato SenML |
| `medidor/ESP32-001/estado` | 5 min | Salud del nodo (RSSI, reconexiones, versión FW, estado ADE/MQTT) |
| `medidor/ESP32-001/alerta` | Evento | Calidad de potencia (sag, swell) |
| `medidor/ESP32-001/conexion` | Al conectar | `"online"` (LWT publica `"offline"` si cae) |

El backend escucha `medidor/#` y almacena todo en PostgreSQL automáticamente.

---

## ID del dispositivo

El ESP32 se identifica como:
```
MQTT Client ID : medidor_cristian_001
device_id en BD: MEDIDOR_CRISTIAN_001   ← (el backend lo convierte a mayúsculas)
```

---

## Desarrollo local

Para levantar el sistema completo localmente:

```cmd
# Terminal 1 — Backend
cd energia-iot-backend
python -m venv venv && venv\Scripts\activate
pip install -r requirements.txt
copy .env.example .env        # Editar con credenciales reales
uvicorn app.main:app --reload
# → http://localhost:8000/docs

# Terminal 2 — Frontend
cd energia-iot-frontend
npm install
npm run dev
# → http://localhost:5173
```

Ver los README individuales de cada carpeta para detalles de configuración.

---

## Flujo de datos completo

1. El **ESP32** mide electricidad con el ADE9153A cada 250ms
2. Cada 60s empaqueta los datos en SenML y los publica via MQTT por NB-IoT
3. El **broker Mosquitto** (Railway) recibe el mensaje
4. El **backend FastAPI** (Railway) tiene un suscriptor MQTT en hilo separado que recibe el mensaje y lo guarda en **PostgreSQL**
5. El **frontend React** (Railway) consulta la REST API del backend cada 30s y actualiza el dashboard
6. El usuario ve los datos en tiempo real en el navegador

---

## Credenciales MQTT (broker en Railway)

```
Host    : shinkansen.proxy.rlwy.net
Puerto  : 58954
Usuario : medidor_iot
Password: Colombia2026$
TLS     : requerido (el proxy Railway usa TLS)
```

Estas credenciales son las mismas que usa el firmware del ESP32 y el backend.
