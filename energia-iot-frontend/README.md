# ⚡ Dashboard Medidor de Energía IoT

Frontend React para visualizar datos del sistema de medición de energía eléctrica.
Tesis de Maestría en IoT - Pontificia Universidad Javeriana.

## 🚀 Instalación Rápida (Windows)

### Requisitos
- Node.js 18+ instalado ([Descargar](https://nodejs.org/))
- Backend FastAPI corriendo en `http://localhost:8000`

### Pasos

```cmd
cd energia-iot-frontend
npm install
npm run dev
```

Abrir: **http://localhost:5173**

## 📁 Estructura

```
src/
├── components/
│   ├── Dashboard.jsx        # Página principal
│   ├── IndicadorEnergia.jsx # Tarjetas de V, A, W
│   ├── GraficoConsumo.jsx   # Gráfico histórico
│   ├── EstadoDispositivo.jsx# Estado del ESP32
│   └── TablaHistorial.jsx   # Tabla de mediciones
├── services/
│   └── api.js               # Conexión con backend
├── App.jsx
├── main.jsx
└── index.css
```

## 🛠️ Tecnologías

- **React 18** - Librería UI
- **Vite** - Build tool ultrarrápido
- **Tailwind CSS** - Estilos utilitarios
- **Recharts** - Gráficos
- **Lucide React** - Iconos

## 📊 Características

- ✅ Indicadores en tiempo real (V, A, W, FP)
- ✅ Gráfico de consumo histórico
- ✅ Estado del dispositivo
- ✅ Tabla de últimas mediciones
- ✅ Auto-actualización cada 30 segundos
- ✅ Diseño responsivo

## ⚙️ Configuración

El frontend se conecta al backend en `http://localhost:8000`.
Para cambiar la URL, edita `src/services/api.js`:

```javascript
const API_BASE_URL = 'http://tu-servidor:puerto/api/v1';
```

## 📝 Notas

- Asegúrate de que el backend esté corriendo antes de iniciar el frontend
- Crea mediciones de prueba usando Swagger (`http://localhost:8000/docs`)
- El dashboard se actualiza automáticamente cada 30 segundos
