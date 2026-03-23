# ⚡ Plataforma IoT - Medidor de Energía Eléctrica

Backend para el sistema de medición de energía eléctrica residencial.
Tesis de Maestría en IoT - Pontificia Universidad Javeriana.

## 🚀 Instalación Rápida (Windows)

```cmd
cd energia-iot-backend
python -m venv venv
venv\Scripts\activate
pip install -r requirements.txt
uvicorn app.main:app --reload
```

Abrir: http://localhost:8000/docs

## 📁 Estructura

```
energia-iot-backend/
├── app/
│   ├── __init__.py
│   ├── config.py        # Variables de entorno
│   ├── database.py      # Conexión BD
│   ├── main.py          # Punto de entrada
│   ├── models/          # Tablas SQLAlchemy
│   ├── schemas/         # Validación Pydantic
│   ├── routers/         # Endpoints API
│   └── services/        # Lógica de negocio
└── requirements.txt
```

## 📊 Endpoints

| Método | Endpoint | Descripción |
|--------|----------|-------------|
| GET | /docs | Swagger UI |
| POST | /api/v1/dispositivos | Crear dispositivo |
| POST | /api/v1/mediciones | Crear medición |
| GET | /api/v1/mediciones/{device}/resumen | Resumen dashboard |
