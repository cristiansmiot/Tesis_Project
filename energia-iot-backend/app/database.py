"""
DATABASE - Conexión a la base de datos con SQLAlchemy
Soporta SQLite para desarrollo local y PostgreSQL para producción (Railway).
"""
import logging
from sqlalchemy import create_engine, text
from sqlalchemy.ext.declarative import declarative_base
from sqlalchemy.orm import sessionmaker, Session
from typing import Generator

from app.config import settings

logger = logging.getLogger(__name__)

# Base para los modelos (siempre disponible)
Base = declarative_base()

# Engine y SessionLocal se inicializan lazy (no al importar)
_engine = None
_SessionLocal = None


def get_database_url() -> str:
    """Determina y normaliza el URL de base de datos."""
    url = settings.DATABASE_URL

    # Normalizar: Railway a veces emite 'postgres://' que SQLAlchemy 2.x no acepta
    if url.startswith("postgres://"):
        url = "postgresql://" + url[len("postgres://"):]

    # Debug: mostrar el URL (con contraseña enmascarada) para diagnóstico
    try:
        from urllib.parse import urlparse
        parsed = urlparse(url)
        safe_url = f"{parsed.scheme}://{parsed.username}:***@{parsed.hostname}:{parsed.port}{parsed.path}"
        logger.info(f"DATABASE_URL procesada: {safe_url}")
        logger.info(f"DATABASE_URL scheme: '{parsed.scheme}', host: '{parsed.hostname}'")
    except Exception as e:
        logger.warning(f"No se pudo parsear URL para debug: {e}")
        logger.info(f"DATABASE_URL raw repr: {repr(url[:50])}")

    if not settings.USE_SQLITE and ("postgresql" in url or "postgres" in url):
        logger.info("Conectando a PostgreSQL (Railway)")
        return url

    logger.warning("Usando SQLite para desarrollo local")
    return "sqlite:///./energia_iot.db"


def _create_engine_safe():
    """Crea el engine con manejo de errores. Retorna SQLite como fallback."""
    database_url = get_database_url()

    try:
        if database_url.startswith("sqlite"):
            engine = create_engine(
                database_url,
                connect_args={"check_same_thread": False},
            )
        else:
            engine = create_engine(
                database_url,
                pool_pre_ping=True,
                pool_size=5,
                max_overflow=10,
                pool_recycle=300,
            )
        logger.info(f"Engine SQLAlchemy creado exitosamente")
        return engine
    except Exception as e:
        logger.error(f"Error creando engine PostgreSQL: {e}")
        logger.error(f"Fallback a SQLite para permitir que la app arranque")
        return create_engine(
            "sqlite:///./energia_iot_fallback.db",
            connect_args={"check_same_thread": False},
        )


def get_engine():
    """Retorna el engine (lazy init)."""
    global _engine
    if _engine is None:
        _engine = _create_engine_safe()
    return _engine


def get_session_local():
    """Retorna SessionLocal (lazy init)."""
    global _SessionLocal
    if _SessionLocal is None:
        _SessionLocal = sessionmaker(autocommit=False, autoflush=False, bind=get_engine())
    return _SessionLocal


# Compatibilidad: alias para código que usa SessionLocal directamente
class _SessionLocalProxy:
    """Proxy lazy para SessionLocal — evita crear engine al importar."""
    def __call__(self, *args, **kwargs):
        return get_session_local()(*args, **kwargs)

    def __getattr__(self, name):
        return getattr(get_session_local(), name)


SessionLocal = _SessionLocalProxy()


def get_db() -> Generator[Session, None, None]:
    """Dependencia que proporciona una sesión de base de datos."""
    db = get_session_local()()
    try:
        yield db
    finally:
        db.close()


def init_db() -> None:
    """Inicializa la base de datos creando todas las tablas."""
    from app.models import dispositivo, medicion, nodo_salud  # noqa: F401
    Base.metadata.create_all(bind=get_engine())
    logger.info("Base de datos inicializada correctamente")


def test_connection() -> bool:
    """Prueba la conexión a la base de datos."""
    try:
        db = get_session_local()()
        db.execute(text("SELECT 1"))
        db.close()
        logger.info("Conexión a la base de datos exitosa")
        return True
    except Exception as e:
        logger.error(f"Error de conexión: {e}")
        return False
