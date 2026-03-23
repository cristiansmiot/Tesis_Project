"""
DATABASE - Conexión a la base de datos con SQLAlchemy
Soporta SQLite para desarrollo local y PostgreSQL para producción (Railway).
"""
from sqlalchemy import create_engine, text
from sqlalchemy.ext.declarative import declarative_base
from sqlalchemy.orm import sessionmaker, Session
from typing import Generator

from app.config import settings


def get_database_url() -> str:
    """
    Determina qué base de datos usar basándose en la configuración.
    """
    # Si USE_SQLITE es False y tenemos URL de PostgreSQL, usar PostgreSQL
    if not settings.USE_SQLITE and "postgresql" in settings.DATABASE_URL:
        print("✅ Conectando a PostgreSQL (Railway)")
        return settings.DATABASE_URL
    
    # Caso contrario, usar SQLite
    print("⚠️  Usando SQLite para desarrollo local")
    return "sqlite:///./energia_iot.db"


def create_db_engine():
    """
    Crea el engine de SQLAlchemy según el tipo de base de datos.
    """
    database_url = get_database_url()
    
    if database_url.startswith("sqlite"):
        return create_engine(
            database_url,
            connect_args={"check_same_thread": False},
            echo=settings.DEBUG
        )
    else:
        return create_engine(
            database_url,
            pool_pre_ping=True,
            pool_size=5,
            max_overflow=10,
            pool_recycle=300,
            echo=settings.DEBUG
        )


# Crear engine
engine = create_db_engine()

# Crear SessionLocal
SessionLocal = sessionmaker(autocommit=False, autoflush=False, bind=engine)

# Base para los modelos
Base = declarative_base()


def get_db() -> Generator[Session, None, None]:
    """Dependencia que proporciona una sesión de base de datos."""
    db = SessionLocal()
    try:
        yield db
    finally:
        db.close()


def init_db() -> None:
    """Inicializa la base de datos creando todas las tablas."""
    from app.models import dispositivo, medicion, nodo_salud  # noqa: F401
    Base.metadata.create_all(bind=engine)
    print("✅ Base de datos inicializada correctamente")


def test_connection() -> bool:
    """Prueba la conexión a la base de datos."""
    try:
        db = SessionLocal()
        db.execute(text("SELECT 1"))
        db.close()
        print("✅ Conexión a la base de datos exitosa")
        return True
    except Exception as e:
        print(f"❌ Error de conexión: {e}")
        return False
