"""ROUTER: Eventos y Alarmas"""
from fastapi import APIRouter, Depends, HTTPException, Query, Request
from pydantic import BaseModel
from sqlalchemy.orm import Session
from sqlalchemy import desc
from typing import Optional, List
from datetime import datetime

from app.database import get_db
from app.models.evento import Evento
from app.models.usuario import Usuario
from app.services.auth import get_current_user, get_optional_user, require_operador_or_admin
from app.services.audit import registrar_accion

router = APIRouter(prefix="/eventos", tags=["Eventos y Alarmas"])


# ── Schemas ──────────────────────────────────────────────────────────────────

class EventoResponse(BaseModel):
    id: int
    device_id: str
    tipo: str
    severidad: str
    valor: Optional[float]
    umbral: Optional[float]
    mensaje: str
    activo: bool
    reconocido_por: Optional[str]
    reconocido_at: Optional[datetime]
    timestamp: Optional[datetime]

    class Config:
        from_attributes = True


class EventoListResponse(BaseModel):
    total: int
    eventos: List[EventoResponse]


# ── Endpoints ────────────────────────────────────────────────────────────────

@router.get("/", response_model=EventoListResponse)
def listar_eventos(
    device_id: Optional[str] = Query(None, description="Filtrar por dispositivo"),
    tipo: Optional[str] = Query(None, description="Filtrar por tipo (sobrevoltaje, subtension, desconexion...)"),
    severidad: Optional[str] = Query(None, description="Filtrar por severidad (info, warning, critical)"),
    activo: Optional[bool] = Query(None, description="Filtrar por estado (true=no reconocida)"),
    skip: int = Query(0, ge=0),
    limit: int = Query(50, ge=1, le=500),
    db: Session = Depends(get_db),
):
    """Listar eventos con filtros opcionales."""
    query = db.query(Evento)

    if device_id:
        query = query.filter(Evento.device_id == device_id)
    if tipo:
        query = query.filter(Evento.tipo == tipo)
    if severidad:
        query = query.filter(Evento.severidad == severidad)
    if activo is not None:
        query = query.filter(Evento.activo == activo)

    total = query.count()
    eventos = query.order_by(desc(Evento.timestamp)).offset(skip).limit(limit).all()

    return EventoListResponse(
        total=total,
        eventos=[EventoResponse.model_validate(e) for e in eventos],
    )


@router.get("/activos", response_model=EventoListResponse)
def listar_alertas_activas(
    limit: int = Query(20, ge=1, le=100),
    db: Session = Depends(get_db),
):
    """Listar alertas activas (no reconocidas)."""
    query = db.query(Evento).filter(Evento.activo == True)
    total = query.count()
    eventos = query.order_by(desc(Evento.timestamp)).limit(limit).all()

    return EventoListResponse(
        total=total,
        eventos=[EventoResponse.model_validate(e) for e in eventos],
    )


@router.patch("/{evento_id}/reconocer")
def reconocer_evento(
    evento_id: int,
    request: Request,
    db: Session = Depends(get_db),
    usuario: Usuario = Depends(require_operador_or_admin),
):
    """Reconocer (resolver) una alerta. Solo operador y super_admin."""
    evento = db.query(Evento).filter(Evento.id == evento_id).first()
    if not evento:
        raise HTTPException(status_code=404, detail="Evento no encontrado")
    if not evento.activo:
        raise HTTPException(status_code=400, detail="El evento ya fue reconocido")

    evento.activo = False
    evento.reconocido_por = usuario.email
    evento.reconocido_at = datetime.utcnow()
    db.commit()

    registrar_accion(
        db, accion="alerta_reconocida", usuario_email=usuario.email,
        recurso="evento", recurso_id=str(evento.id),
        detalles={"device_id": evento.device_id, "tipo": evento.tipo},
        ip_address=request.client.host if request.client else None,
    )

    return {"mensaje": "Evento reconocido", "evento_id": evento_id}
