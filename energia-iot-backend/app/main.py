"""
MAIN - Punto de Entrada de la Aplicación FastAPI

Para ejecutar: uvicorn app.main:app --reload
Documentación: http://localhost:8000/docs
"""
from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware
from contextlib import asynccontextmanager
import logging

from app.config import settings
from app.database import init_db, get_session_local
from app.routers import (
    dispositivos_router, mediciones_router, salud_router,
    auth_router, eventos_router, comandos_router,
    auditoria_router, dashboard_router,
)
from app.services.mqtt_client import create_mqtt_client, get_mqtt_client

logging.basicConfig(level=getattr(logging, settings.LOG_LEVEL), format="%(asctime)s - %(levelname)s - %(message)s")
logger = logging.getLogger(__name__)


@asynccontextmanager
async def lifespan(app: FastAPI):
    logger.info("🚀 Iniciando aplicación...")
    logger.info(f"📌 Entorno: {settings.ENVIRONMENT}")
    logger.info(f"📌 Base de datos: {'PostgreSQL' if not settings.USE_SQLITE else 'SQLite'}")
    try:
        init_db()
        # Crear usuario admin por defecto si no existe
        from app.services.auth import create_default_admin
        from app.database import get_session_local
        db_session = get_session_local()()
        try:
            create_default_admin(db_session)
        finally:
            db_session.close()
    except Exception as e:
        logger.error(f"❌ Error inicializando BD: {e} (la API arrancará sin BD)")
        # No raise: permitir que la app suba y responda /health aunque BD falle al inicio

    # === Iniciar cliente MQTT ===
    mqtt_broker = settings.MQTT_BROKER
    mqtt_port = settings.MQTT_PORT
    mqtt_username = settings.MQTT_USERNAME
    mqtt_password = settings.MQTT_PASSWORD
    mqtt_topic = settings.MQTT_TOPIC_BASE
    mqtt_client_id = settings.MQTT_CLIENT_ID

    mqtt_use_tls = settings.MQTT_USE_TLS

    try:
        client = create_mqtt_client(
            broker_host=mqtt_broker,
            broker_port=mqtt_port,
            username=mqtt_username,
            password=mqtt_password,
            topic_base=mqtt_topic,
            client_id=mqtt_client_id,
            use_tls=mqtt_use_tls,
        )
        client.set_db_session_factory(get_session_local())
        client.start()
        logger.info(f"📡 Cliente MQTT conectando a {mqtt_broker}:{mqtt_port} (TLS={'sí' if mqtt_use_tls else 'no'})")
    except Exception as e:
        logger.warning(f"⚠️ No se pudo iniciar MQTT: {e} (la API seguirá funcionando sin MQTT)")

    logger.info("✅ Aplicación lista")
    yield

    # === Detener cliente MQTT ===
    mqtt = get_mqtt_client()
    if mqtt:
        mqtt.stop()
    logger.info("🛑 Aplicación cerrada")


app = FastAPI(
    title="⚡ API Medidor de Energía IoT",
    description="Sistema IoT para medición de energía - Tesis Maestría Javeriana",
    version="1.0.0",
    lifespan=lifespan,
)

app.add_middleware(
    CORSMiddleware,
    allow_origins=settings.cors_origins_list,
    allow_origin_regex=r"https://.*\.up\.railway\.app",
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

app.include_router(auth_router, prefix=settings.API_PREFIX)
app.include_router(dispositivos_router, prefix=settings.API_PREFIX)
app.include_router(mediciones_router, prefix=settings.API_PREFIX)
app.include_router(salud_router, prefix=settings.API_PREFIX)
app.include_router(eventos_router, prefix=settings.API_PREFIX)
app.include_router(comandos_router, prefix=settings.API_PREFIX)
app.include_router(auditoria_router, prefix=settings.API_PREFIX)
app.include_router(dashboard_router, prefix=settings.API_PREFIX)


@app.get("/", tags=["Root"])
def root():
    return {"nombre": "API Medidor de Energía IoT", "version": "1.0.0", "docs": "/docs", "estado": "✅ Operativo"}


@app.get("/health", tags=["Health"])
def health_check():
    from datetime import datetime
    return {"status": "healthy", "timestamp": datetime.utcnow().isoformat(), "database": "PostgreSQL" if not settings.USE_SQLITE else "SQLite"}


@app.get("/api/v1/mqtt/status", tags=["MQTT"])
def mqtt_status():
    """Retorna el estado actual de la conexión MQTT."""
    client = get_mqtt_client()
    if client:
        return client.get_status()
    return {"connected": False, "error": "Cliente MQTT no inicializado"}
