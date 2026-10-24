"""
DATABASE - Conexión a la base de datos con SQLAlchemy
Soporta SQLite para desarrollo local y PostgreSQL para producción (Railway).
"""
import logging
from sqlalchemy import create_engine, text, inspect
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
    """
    Determina y normaliza el URL de base de datos.

    Auto-detección: si DATABASE_URL contiene 'postgres', usa PostgreSQL
    independientemente de USE_SQLITE. Esto evita errores de configuración
    en Railway donde USE_SQLITE podría quedar en True por defecto.
    """
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

    # Auto-detección: si la URL contiene 'postgres', SIEMPRE usar PostgreSQL
    # (ignora USE_SQLITE para evitar configuración incorrecta en producción)
    is_postgres = "postgresql" in url or "postgres" in url

    if is_postgres:
        logger.info("Conectando a PostgreSQL (auto-detectado desde DATABASE_URL)")
        return url

    if not settings.USE_SQLITE:
        logger.warning("USE_SQLITE=False pero DATABASE_URL no contiene 'postgres'. Usando SQLite como fallback.")

    logger.info("Usando SQLite para desarrollo local")
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
    from app.models import dispositivo, medicion, nodo_salud, usuario, evento, audit_log  # noqa: F401
    Base.metadata.create_all(bind=get_engine())
    _aplicar_migraciones_incrementales()
    _reconciliar_eventos_historicos()
    logger.info("Base de datos inicializada correctamente")


def _reconciliar_eventos_historicos() -> None:
    """Cierra alarmas antiguas que ya tienen evidencia posterior de recuperación.

    Versiones anteriores confundían ``activo`` con ``no reconocido`` y además
    guardaban CLEARED como una fila separada. Esta migración lógica es
    idempotente y conserva todas las filas como evidencia histórica.
    """
    from app.models.evento import Evento

    db = get_session_local()()
    try:
        recuperaciones = (db.query(Evento)
            .filter((Evento.transition == "cleared") |
                    (Evento.tipo.in_(["module_ready", "time_synced", "ac_restored"])))
            .order_by(Evento.timestamp.asc(), Evento.id.asc()).all())
        for rec in recuperaciones:
            fin = rec.ts_device or rec.timestamp or rec.created_at
            q = db.query(Evento).filter(
                Evento.device_id == rec.device_id,
                Evento.activo.is_(True),
                Evento.timestamp <= rec.timestamp,
            )
            if rec.transition == "cleared":
                q = q.filter(Evento.tipo == rec.tipo)
                if rec.module:
                    q = q.filter(Evento.module == rec.module)
                if rec.code:
                    q = q.filter(Evento.code == rec.code)
            elif rec.tipo == "module_ready" and rec.module:
                q = q.filter(Evento.module == rec.module)
            elif rec.tipo == "time_synced":
                q = q.filter(Evento.tipo.in_([
                    "time_invalid", "time_sync_failed", "rtc_backup_lost",
                ]))
            elif rec.tipo == "ac_restored":
                q = q.filter(Evento.tipo == "ac_lost")
            else:
                continue
            q.update({Evento.activo: False, Evento.fin_at: fin},
                     synchronize_session=False)
        db.commit()
    except Exception:
        db.rollback()
        logger.exception("No se pudieron reconciliar los eventos históricos")
    finally:
        db.close()


def _aplicar_migraciones_incrementales(engine=None) -> None:
    """Columnas agregadas después del despliegue inicial.

    create_all solo crea tablas nuevas, no altera las existentes, así que
    la BD de producción necesita estos ALTER. Son idempotentes
    (IF NOT EXISTS) y correr en cada arranque no tiene efecto si ya
    aplicaron. SQLite local se recrea completo con create_all.
    """
    target = engine or get_engine()
    # Inspection works for existing SQLite files as well as PostgreSQL.
    columns = {
        "dispositivos": {"etiqueta": "VARCHAR(50)"},
        "mediciones": {
            "boot_id": "BIGINT", "seq": "BIGINT", "fw_version": "VARCHAR(20)",
            "uptime_ms": "BIGINT", "time_valid": "BOOLEAN",
            "ts_device": "TIMESTAMP WITH TIME ZONE", "ts_server": "TIMESTAMP WITH TIME ZONE",
            "last_received_at": "TIMESTAMP WITH TIME ZONE", "calibration_status": "INTEGER",
            "raw_payload": "TEXT", "payload_sha256": "VARCHAR(64)",
            "duplicate_count": "INTEGER NOT NULL DEFAULT 0",
            "identity_conflict_count": "INTEGER NOT NULL DEFAULT 0",
        },
        "nodo_salud": {
            "radio_valid": "BOOLEAN", "rsrp_dbm": "FLOAT", "rsrq_db": "FLOAT",
            "rssnr_raw": "FLOAT", "sinr_db": "FLOAT", "boot_id": "BIGINT",
            "uptime_ms": "BIGINT", "ts_device": "TIMESTAMP WITH TIME ZONE",
            "time_valid": "BOOLEAN", "data_generated_count": "BIGINT",
            "data_publish_attempts": "BIGINT", "data_publish_ok": "BIGINT",
            "ac_loss_count": "INTEGER", "ac_restore_count": "INTEGER",
            "reset_reason": "INTEGER", "reset_reason_name": "VARCHAR(32)",
            "cereg_stat": "INTEGER", "cnact_state": "INTEGER",
            "smstate": "INTEGER", "last_publish_error": "INTEGER",
            "last_recovery_error": "INTEGER", "last_recovery_ms": "INTEGER",
            "recovery_stage": "VARCHAR(24)",
        },
        "eventos": {
            "event_id": "VARCHAR(24)", "module": "VARCHAR(24)",
            "code": "VARCHAR(50)", "transition": "VARCHAR(20)",
            "boot_id": "BIGINT", "seq": "BIGINT", "uptime_ms": "BIGINT",
            "occurrence_count": "INTEGER", "error": "INTEGER",
            "fw_version": "VARCHAR(20)", "time_valid": "BOOLEAN",
            "ts_device": "TIMESTAMP WITH TIME ZONE",
            "fin_at": "TIMESTAMP WITH TIME ZONE",
        },
    }
    with target.begin() as conn:
        inspector = inspect(conn)
        for table, additions in columns.items():
            if not inspector.has_table(table):
                continue
            existing = {c["name"] for c in inspector.get_columns(table)}
            for name, sql_type in additions.items():
                if name not in existing:
                    guard = "IF NOT EXISTS " if target.dialect.name == "postgresql" else ""
                    conn.execute(text(f"ALTER TABLE {table} ADD COLUMN {guard}{name} {sql_type}"))
        conn.execute(text("CREATE INDEX IF NOT EXISTS ix_dispositivos_etiqueta ON dispositivos (etiqueta)"))
        conn.execute(text("CREATE UNIQUE INDEX IF NOT EXISTS uq_mediciones_device_boot_seq ON mediciones (device_id, boot_id, seq)"))
        if inspector.has_table("eventos"):
            conn.execute(text("CREATE UNIQUE INDEX IF NOT EXISTS uq_eventos_device_event_id ON eventos (device_id, event_id)"))


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
