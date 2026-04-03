"""ROUTER: Dashboard - Endpoints de agregación para el resumen general"""
from fastapi import APIRouter, Depends, Query
from pydantic import BaseModel
from sqlalchemy.orm import Session
from sqlalchemy import func, desc, and_
from typing import Optional, List
from datetime import datetime, timedelta, timezone

from app.database import get_db
from app.models.dispositivo import Dispositivo
from app.models.medicion import Medicion
from app.models.evento import Evento

router = APIRouter(prefix="/dashboard", tags=["Dashboard"])


# ── Schemas ──────────────────────────────────────────────────────────────────

class ResumenGeneral(BaseModel):
    total_dispositivos: int
    online: int
    offline: int
    alarmas_activas: int
    consumo_total_kwh: float


class ConsumoHorario(BaseModel):
    hora: str
    consumo: float  # kWh


class ConsumoHorarioResponse(BaseModel):
    fecha: str
    datos: List[ConsumoHorario]


# ── Endpoints ────────────────────────────────────────────────────────────────

@router.get("/resumen", response_model=ResumenGeneral)
def obtener_resumen(db: Session = Depends(get_db)):
    """
    Obtener métricas consolidadas para el dashboard principal.
    Retorna conteo de dispositivos, alarmas activas y consumo total del día.
    """
    # Conteo de dispositivos
    total = db.query(func.count(Dispositivo.id)).filter(Dispositivo.activo == True).scalar() or 0
    online = db.query(func.count(Dispositivo.id)).filter(
        Dispositivo.activo == True, Dispositivo.conectado == True
    ).scalar() or 0
    offline = total - online

    # Alarmas activas
    alarmas = db.query(func.count(Evento.id)).filter(Evento.activo == True).scalar() or 0

    # Consumo total del día: suma de la última energía reportada por cada dispositivo
    # Subconsulta: última medición por dispositivo
    from sqlalchemy.orm import aliased
    subq = (
        db.query(
            Medicion.device_id,
            func.max(Medicion.id).label("max_id"),
        )
        .group_by(Medicion.device_id)
        .subquery()
    )
    consumo_total = (
        db.query(func.coalesce(func.sum(Medicion.energia_activa), 0.0))
        .join(subq, Medicion.id == subq.c.max_id)
        .scalar()
    ) or 0.0

    return ResumenGeneral(
        total_dispositivos=total,
        online=online,
        offline=offline,
        alarmas_activas=alarmas,
        consumo_total_kwh=round(consumo_total, 1),
    )


@router.get("/consumo-horario", response_model=ConsumoHorarioResponse)
def obtener_consumo_horario(
    horas: int = Query(24, ge=1, le=168, description="Horas hacia atrás"),
    db: Session = Depends(get_db),
):
    """
    Obtener consumo agregado por hora para la gráfica de barras del dashboard.
    Agrupa la potencia activa promedio por hora y la convierte a kWh.
    """
    desde = datetime.now(timezone.utc) - timedelta(hours=horas)

    # Obtener mediciones de las últimas N horas
    mediciones = (
        db.query(Medicion.timestamp, Medicion.potencia_activa)
        .filter(Medicion.timestamp >= desde)
        .order_by(Medicion.timestamp)
        .all()
    )

    # Agrupar por hora
    horas_bucket = {}
    for h in range(24):
        horas_bucket[h] = []

    for m in mediciones:
        if m.timestamp and m.potencia_activa is not None:
            hora = m.timestamp.hour
            horas_bucket[hora].append(m.potencia_activa)

    # Calcular consumo promedio por hora (W promedio → kWh en 1 hora)
    datos = []
    for h in range(24):
        valores = horas_bucket[h]
        if valores:
            promedio_w = sum(valores) / len(valores)
            kwh = promedio_w / 1000.0  # W promedio * 1h = Wh, /1000 = kWh
        else:
            kwh = 0.0
        datos.append(ConsumoHorario(
            hora=f"{h:02d}:00",
            consumo=round(kwh, 2),
        ))

    return ConsumoHorarioResponse(
        fecha=datetime.now(timezone.utc).strftime("%Y-%m-%d"),
        datos=datos,
    )
