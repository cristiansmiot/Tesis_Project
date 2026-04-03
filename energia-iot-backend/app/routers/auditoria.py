"""ROUTER: Auditoría - Registro de actividades del sistema"""
from fastapi import APIRouter, Depends, Query
from pydantic import BaseModel
from sqlalchemy.orm import Session
from sqlalchemy import desc
from typing import Optional, List
from datetime import datetime

from app.database import get_db
from app.models.audit_log import AuditLog
from app.models.usuario import Usuario
from app.services.auth import get_current_user, require_operador_or_admin

router = APIRouter(prefix="/auditoria", tags=["Auditoría"])


# ── Schemas ──────────────────────────────────────────────────────────────────

class AuditLogResponse(BaseModel):
    id: int
    usuario_email: Optional[str]
    accion: str
    recurso: Optional[str]
    recurso_id: Optional[str]
    detalles: Optional[str]
    ip_address: Optional[str]
    timestamp: Optional[datetime]

    class Config:
        from_attributes = True


class AuditListResponse(BaseModel):
    total: int
    registros: List[AuditLogResponse]


# ── Endpoints ────────────────────────────────────────────────────────────────

@router.get("/", response_model=AuditListResponse)
def listar_auditoria(
    usuario_email: Optional[str] = Query(None, description="Filtrar por usuario"),
    accion: Optional[str] = Query(None, description="Filtrar por tipo de acción"),
    recurso: Optional[str] = Query(None, description="Filtrar por tipo de recurso"),
    skip: int = Query(0, ge=0),
    limit: int = Query(50, ge=1, le=500),
    db: Session = Depends(get_db),
    usuario: Usuario = Depends(require_operador_or_admin),
):
    """
    Listar registros de auditoría.
    - super_admin: ve toda la auditoría
    - operador: solo ve sus propias acciones
    - visualizador: no tiene acceso (bloqueado por require_operador_or_admin)
    """
    query = db.query(AuditLog)

    # Operador solo ve sus propias acciones
    if usuario.es_operador:
        query = query.filter(AuditLog.usuario_email == usuario.email)
    elif usuario_email:
        # Super admin puede filtrar por cualquier usuario
        query = query.filter(AuditLog.usuario_email == usuario_email)

    if accion:
        query = query.filter(AuditLog.accion == accion)
    if recurso:
        query = query.filter(AuditLog.recurso == recurso)

    total = query.count()
    registros = query.order_by(desc(AuditLog.timestamp)).offset(skip).limit(limit).all()

    return AuditListResponse(
        total=total,
        registros=[AuditLogResponse.model_validate(r) for r in registros],
    )
