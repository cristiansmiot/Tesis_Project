"""Dashboard - Endpoints de agregación para el resumen general"""
from fastapi import APIRouter, Depends, Query
from pydantic import BaseModel
from sqlalchemy.orm import Session
from sqlalchemy import func
from typing import Optional, List
from datetime import datetime, timezone

from app.config import settings
from app.database import get_db
from app.services.conexion import refrescar_estados_conexion
from app.services.consumo import BOGOTA, consumo_por_hora, consumo_wh, inicio_dia_local
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
    # Consumo de hoy (kWh) desde las 00:00 de Bogotá: diferencia del contador
    # de cada medidor, no la suma de los contadores. consumo_total_kwh se
    # conserva como alias para clientes anteriores.
    consumo_total_kwh: float
    consumo_dia_kwh: float = 0.0
    consumo_dia_desde: Optional[datetime] = None
    # Suma del último contador de cada medidor activo (kWh).
    energia_acumulada_kwh: Optional[float] = None
    # Indicador residencial: 120 V nominales, banda estacionaria 108-132 V.
    # promedio de la última medición de cada dispositivo y cuántos están
    # fuera del rango admisible. Si no hay mediciones, voltaje_promedio es None.
    voltaje_promedio: Optional[float] = None
    dispositivos_fuera_rango: int = 0
    voltaje_min_creg: float = settings.voltaje_min
    voltaje_max_creg: float = settings.voltaje_max


class ConsumoHorario(BaseModel):
    hora: str  # "HH:00", hora local de Bogotá
    consumo: float  # kWh
    wh: float = 0.0
    inicio: Optional[datetime] = None  # inicio de la hora (UTC)
    muestras: int = 0  # lecturas recibidas en esa hora
    con_hueco: bool = False  # incluye consumo de un periodo sin datos > 1 h


class ConsumoHorarioResponse(BaseModel):
    fecha: str  # fecha local de Bogotá
    zona_horaria: str = "America/Bogota (UTC-5)"
    datos: List[ConsumoHorario]


# ── Endpoints ────────────────────────────────────────────────────────────────

@router.get("/resumen", response_model=ResumenGeneral)
def obtener_resumen(db: Session = Depends(get_db)):
    """
    Obtener métricas consolidadas para el dashboard principal.
    Retorna conteo de dispositivos, alarmas activas y consumo de hoy (kWh).
    """
    # Antes de contar, apagar el flag de los que dejaron de publicar:
    # La presencia depende de last_seen, no de mensajes LWT.
    refrescar_estados_conexion(db)

    # Conteo de dispositivos
    total = db.query(func.count(Dispositivo.id)).filter(Dispositivo.activo == True).scalar() or 0
    online = db.query(func.count(Dispositivo.id)).filter(
        Dispositivo.activo == True, Dispositivo.conectado == True
    ).scalar() or 0
    offline = total - online

    # Alarmas activas
    alarmas = db.query(func.count(Evento.id)).filter(Evento.activo == True).scalar() or 0

    # Consumo de hoy: diferencias del contador desde las 00:00 de Bogotá.
    ahora = datetime.now(timezone.utc)
    inicio_dia = inicio_dia_local(ahora)
    consumo_dia_kwh = round(consumo_wh(db, inicio_dia, ahora) / 1000.0, 4)

    # Última lectura de cada medidor activo, por marca de tiempo y no por id:
    # el backlog de la microSD llega después con lecturas más antiguas.
    ultimas = []
    for (device_id,) in db.query(Dispositivo.device_id).filter(Dispositivo.activo == True).all():
        ultima = (
            db.query(Medicion.energia_activa, Medicion.voltaje_rms)
            .filter(Medicion.device_id == device_id)
            .order_by(Medicion.timestamp.desc(), Medicion.id.desc())
            .first()
        )
        if ultima is not None:
            ultimas.append(ultima)
    contadores = [e for e, _ in ultimas if e is not None]
    energia_acumulada_kwh = round(sum(contadores) / 1000.0, 3) if contadores else None

    # Tensión de la red: última lectura de voltaje por dispositivo.
    # Se descartan lecturas en 0 (nodo encendido pero sin línea AC) para no
    # arrastrar el promedio ni contar falsos fuera-de-rango.
    voltajes = [v for _, v in ultimas if v and v > 0]
    voltaje_promedio = round(sum(voltajes) / len(voltajes), 1) if voltajes else None
    fuera_rango = sum(
        1 for v in voltajes if v < settings.voltaje_min or v > settings.voltaje_max
    )

    return ResumenGeneral(
        total_dispositivos=total,
        online=online,
        offline=offline,
        alarmas_activas=alarmas,
        consumo_total_kwh=consumo_dia_kwh,
        consumo_dia_kwh=consumo_dia_kwh,
        consumo_dia_desde=inicio_dia,
        energia_acumulada_kwh=energia_acumulada_kwh,
        voltaje_promedio=voltaje_promedio,
        dispositivos_fuera_rango=fuera_rango,
        voltaje_min_creg=settings.voltaje_min,
        voltaje_max_creg=settings.voltaje_max,
    )


@router.get("/consumo-horario", response_model=ConsumoHorarioResponse)
def obtener_consumo_horario(
    horas: int = Query(24, ge=1, le=168, description="Horas hacia atrás"),
    db: Session = Depends(get_db),
):
    """
    Consumo agregado de todos los medidores por hora local de Bogotá, de la
    hora ``horas - 1`` atrás hasta la actual (en curso). Cada barra es la
    diferencia del contador de energía, sumada entre medidores.
    """
    ahora = datetime.now(timezone.utc)
    datos = [
        ConsumoHorario(
            hora=c["inicio"].astimezone(BOGOTA).strftime("%H:00"),
            consumo=round(c["wh"] / 1000.0, 4),
            wh=round(c["wh"], 2),
            inicio=c["inicio"],
            muestras=c["muestras"],
            con_hueco=c["con_hueco"],
        )
        for c in consumo_por_hora(db, horas, ahora)
    ]
    return ConsumoHorarioResponse(
        fecha=ahora.astimezone(BOGOTA).strftime("%Y-%m-%d"),
        datos=datos,
    )
