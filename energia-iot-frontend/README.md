# Frontend — Plataforma de Monitoreo de Energía

Interfaz web del sistema de medición de energía residencial.
Trabajo de grado, Maestría en Ingeniería de IoT — Pontificia Universidad Javeriana.

React 18 + Vite 5, Tailwind CSS para estilos y Recharts para gráficas.
Autenticación contra el backend FastAPI (JWT) con tres roles que condicionan
la UI: `visualizador` no ve la tab Comandos ni la gestión de usuarios;
`operador` puede enviar comandos; `super_admin` además administra usuarios.

## Vistas

- **Resumen** (`pages/Resumen.jsx`): KPIs de la flota (medidores online/offline,
  alarmas, consumo del día, tensión promedio de red con badge CREG), consumo
  agregado por hora y alertas activas.
- **Medidores** (`pages/Medidores.jsx`): listado con estado de conexión y señal.
- **Detalle** (`pages/MedidorDetalle.jsx`): tabs Resumen / Variables /
  Histórico / Comandos / Eventos. La gráfica de voltaje dibuja las líneas de
  referencia CREG 024/2015 (99 V y 121 V). Los comandos (reiniciar, corte,
  restaurar, sincronizar hora) piden confirmación y quedan auditados en el
  backend.
- **Eventos / Auditoría / Usuarios / Perfil**: gestión operativa.

## Umbrales de tensión

`src/utils/voltage.js` centraliza la lógica CREG 024/2015: nominal 110 V
±10 % (99–121 V). Si se cambia el nominal hay que cambiarlo también en el
backend (`app/config.py`), que es quien genera los eventos persistidos —
el frontend solo colorea.

## Ejecución local

Requiere Node 18+ y el backend corriendo en `http://localhost:8000`.

```cmd
npm install
npm run dev
```

La URL del backend se resuelve en `src/services/api.js` (variable de entorno
`VITE_API_URL` en producción).

## Estructura

```
src/
├── pages/            # Una por ruta (ver App.jsx)
├── components/
│   ├── common/       # KpiCard, StatusBadge, TabNav, ConfirmDialog
│   ├── charts/       # ConsumoAgregadoChart, ConsumoHistoricoChart
│   ├── device/       # Paneles del detalle de medidor
│   ├── alerts/       # Lista de alertas activas
│   ├── sidebar/ topbar/
│   ├── MetricasTransmision.jsx   # Confiabilidad del enlace celular
│   └── TablaHistorial.jsx
├── contexts/AuthContext.jsx      # Sesión JWT + permisos por rol
├── layouts/          # AppLayout (protegido) y AuthLayout (login)
├── services/api.js   # Cliente axios con interceptor de token
└── utils/voltage.js  # Umbrales CREG 024/2015
```

## Despliegue

Railway, build estático servido con `serve`.
Producción: https://intuitive-generosity-production-bfb2.up.railway.app
