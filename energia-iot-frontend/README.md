# Frontend - Dashboard Medidor de Energía IoT

React + Tailwind CSS + Recharts para visualizar datos del sistema de medición.
Tesis de Maestría en IoT — Pontificia Universidad Javeriana.

---

## Acceso en producción (Railway)

El frontend está desplegado en Railway y se conecta automáticamente al backend.

### Encontrar la URL de tu servicio

1. Ir a [railway.app](https://railway.app) → tu proyecto
2. Hacer clic en el servicio **energia-iot-frontend**
3. Pestaña **Settings** → sección **Domains** → copiar la URL pública

La URL tiene el formato:
```
https://energia-iot-frontend-production-XXXX.up.railway.app
```

Abrir esa URL en el navegador → el dashboard carga directamente.

### Verificar que el frontend está conectado al backend

Dentro del dashboard, busca el indicador de estado en la parte superior.
Si muestra datos del dispositivo `MEDIDOR_CRISTIAN_001` significa que la conexión backend-frontend funciona.

Si el dashboard aparece en blanco o con error:
1. Verificar que la variable `VITE_API_URL` está configurada en Railway (ver sección Variables)
2. Verificar que el backend también está "Online" en Railway

---

## Qué muestra el dashboard

| Componente | Datos mostrados |
|-----------|----------------|
| **IndicadorEnergia** | Voltaje (V), Corriente (A), Potencia activa (W), Factor de potencia |
| **EstadoDispositivo** | Online/Offline, última conexión, tiempo sin datos |
| **GraficoConsumo** | Histórico de potencia y voltaje (últimas 24h) |
| **TablaHistorial** | Tabla de las últimas mediciones con timestamp |

El dashboard se **actualiza automáticamente cada 30 segundos**.

---

## Ejecutar localmente (desarrollo)

### Requisitos
- Node.js 18+ ([Descargar](https://nodejs.org/))
- Backend corriendo en `http://localhost:8000` (ver README del backend)

### Pasos

```cmd
cd energia-iot-frontend
npm install
npm run dev
```

Abrir: **http://localhost:5173**

En desarrollo el frontend apunta a `http://localhost:8000` automáticamente
(configurado en `vite.config.js` con proxy `/api`).

---

## Conectar a un backend remoto (Railway) desde local

Si quieres correr el frontend localmente pero contra el backend de Railway:

Crear archivo `.env.local` en la raíz del frontend:
```env
VITE_API_URL=https://TU-URL-BACKEND.up.railway.app
```

Luego reiniciar con `npm run dev`. El frontend usará el backend de Railway.

---

## Variables de entorno en Railway

Configurar en: Railway → servicio frontend → pestaña **Variables**

| Variable | Valor |
|----------|-------|
| `VITE_API_URL` | URL del backend Railway, sin trailing slash. Ej: `https://energia-iot-backend-production-xxxx.up.railway.app` |

Sin esta variable, el frontend en Railway no sabe dónde está el backend y las peticiones fallan.

---

## Estructura del proyecto

```
energia-iot-frontend/
├── src/
│   ├── components/
│   │   ├── Dashboard.jsx          # Página principal, coordina todos los componentes
│   │   ├── IndicadorEnergia.jsx   # Tarjetas V, A, W, FP
│   │   ├── GraficoConsumo.jsx     # Gráfico histórico (Recharts)
│   │   ├── EstadoDispositivo.jsx  # Estado de conexión del ESP32
│   │   └── TablaHistorial.jsx     # Tabla de mediciones recientes
│   ├── services/
│   │   └── api.js                 # Funciones fetch hacia el backend
│   ├── App.jsx
│   ├── main.jsx
│   └── index.css                  # Tailwind CSS
├── public/
├── .env.example                   # Plantilla de variables de entorno
├── railway.toml                   # Config de despliegue Railway
├── vite.config.js                 # Config Vite + proxy desarrollo
├── tailwind.config.js
└── package.json
```

---

## Endpoints del backend que usa el frontend

El archivo `src/services/api.js` hace llamadas a:

```
GET /api/v1/dispositivos/                           → lista dispositivos
GET /api/v1/dispositivos/{id}/estado                → estado de conexión
GET /api/v1/dispositivos/{id}/salud                 → salud del nodo (RSSI, reconexiones)
GET /api/v1/mediciones/{id}/ultima                  → última medición
GET /api/v1/mediciones/{id}/historico?horas=24      → histórico + estadísticas
GET /api/v1/salud/{id}/metricas?horas=24            → PDR, RSSI, msg_tx
GET /health                                          → health check del backend
```

El `device_id` del ESP32 en el sistema es: **`MEDIDOR_CRISTIAN_001`**

---

## Tecnologías

- **React 18** — UI declarativa
- **Vite 5** — Build tool y dev server
- **Tailwind CSS** — Estilos utilitarios
- **Recharts** — Gráficos SVG responsivos
- **Lucide React** — Iconos
- **serve** — Servidor estático para producción Railway
