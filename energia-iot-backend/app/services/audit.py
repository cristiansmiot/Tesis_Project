"""
SERVICIO: Registro de auditoría
Utilidad para registrar acciones del sistema.
"""
import json
import logging
from typing import Optional
from sqlalchemy.orm import Session
from app.models.audit_log import AuditLog

logger = logging.getLogger(__name__)


def registrar_accion(
    db: Session,
    accion: str,
    usuario_email: Optional[str] = None,
    recurso: Optional[str] = None,
    recurso_id: Optional[str] = None,
    detalles: Optional[dict] = None,
    ip_address: Optional[str] = None,
):
    """
    Registra una acción en la tabla de auditoría.

    Args:
        db: Sesión de base de datos
        accion: Tipo de acción (login, logout, comando_enviado, etc.)
        usuario_email: Email del usuario que realizó la acción
        recurso: Tipo de recurso afectado (dispositivo, evento, usuario)
        recurso_id: ID del recurso afectado
        detalles: Diccionario con información adicional
        ip_address: Dirección IP del cliente
    """
    try:
        log = AuditLog(
            usuario_email=usuario_email,
            accion=accion,
            recurso=recurso,
            recurso_id=recurso_id,
            detalles=json.dumps(detalles, default=str) if detalles else None,
            ip_address=ip_address,
        )
        db.add(log)
        db.commit()
        logger.debug(f"📝 Auditoría: {accion} por {usuario_email or 'sistema'}")
    except Exception as e:
        logger.error(f"❌ Error registrando auditoría: {e}")
        db.rollback()
