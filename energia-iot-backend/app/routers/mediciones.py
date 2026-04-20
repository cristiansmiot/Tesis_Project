"""ROUTER: Mediciones - CRUD de mediciones de energía"""
from fastapi import APIRouter, Depends, HTTPException, Query, status
from sqlalchemy.orm import Session
from sqlalchemy import func, desc, asc
from typing import List, Optional
from datetime import datetime, timedelta, timezone

from app.database import get_db
from app.models.dispositivo import Dispositivo
from app.models.medicion import Medicion
from app.schemas.medicion import MedicionCreate, MedicionResponse, MedicionResumen, MedicionHistorico

router = APIRouter(prefix="/mediciones", tags=["Mediciones"])


@router.get("/", response_model=List[MedicionResponse])
def listar_mediciones(skip: int = 0, limit: int = 100, device_id: Optional[str] = None, db: Session = Depends(get_db)):
    query = db.query(Medicion)
    if device_id:
        query = query.filter(Medicion.device_id == device_id.strip().upper())
    return query.order_by(desc(Medicion.timestamp)).offset(skip).limit(limit).all()


@router.get("/{medicion_id}", response_model=MedicionResponse)
def obtener_medicion(medicion_id: int, db: Session = Depends(get_db)):
    medicion = db.query(Medicion).filter(Medicion.id == medicion_id).first()
    if not medicion:
        raise HTTPException(status_code=404, detail=f"Medición {medicion_id} no encontrada")
    return medicion


@router.post("/", response_model=MedicionResponse, status_code=status.HTTP_201_CREATED)
def crear_medicion(data: MedicionCreate, db: Session = Depends(get_db)):
    dispositivo = db.query(Dispositivo).filter(Dispositivo.device_id == data.device_id).first()
    if not dispositivo:
        dispositivo = Dispositivo(device_id=data.device_id, nombre=f"Auto-registrado: {data.device_id}")
        db.add(dispositivo)
        db.commit()
        db.refresh(dispositivo)
    dispositivo.conectado = True
    dispositivo.ultima_conexion = datetime.utcnow()
    nueva = Medicion(
        dispositivo_id=dispositivo.id, device_id=data.device_id, voltaje_rms=data.voltaje_rms,
        corriente_rms=data.corriente_rms, potencia_activa=data.potencia_activa,
        potencia_reactiva=data.potencia_reactiva, potencia_aparente=data.potencia_aparente,
        factor_potencia=data.factor_potencia, frecuencia=data.frecuencia, thd_voltaje=data.thd_voltaje,
        thd_corriente=data.thd_corriente, energia_activa=data.energia_activa,
        energia_reactiva=data.energia_reactiva, timestamp=data.timestamp or datetime.utcnow()
    )
    db.add(nueva)
    db.commit()
    db.refresh(nueva)
    return nueva


@router.get("/{device_id}/ultima", response_model=MedicionResponse)
def obtener_ultima_medicion(device_id: str, db: Session = Depends(get_db)):
    device_id = device_id.strip().upper()
    medicion = db.query(Medicion).filter(Medicion.device_id == device_id).order_by(desc(Medicion.timestamp)).first()
    if not medicion:
        raise HTTPException(status_code=404, detail=f"No hay mediciones para '{device_id}'")
    return medicion


@router.get("/{device_id}/resumen", response_model=MedicionResumen)
def obtener_resumen(device_id: str, db: Session = Depends(get_db)):
    device_id = device_id.strip().upper()
    medicion = db.query(Medicion).filter(Medicion.device_id == device_id).order_by(desc(Medicion.timestamp)).first()
    if not medicion:
        raise HTTPException(status_code=404, detail=f"No hay mediciones para '{device_id}'")
    return MedicionResumen(
        device_id=medicion.device_id, voltaje=f"{medicion.voltaje_rms:.1f} V",
        corriente=f"{medicion.corriente_rms:.3f} A", potencia=f"{medicion.potencia_activa:.1f} W",
        factor_potencia=f"{medicion.factor_potencia:.2f}" if medicion.factor_potencia else "N/A",
        energia_kwh=f"{medicion.energia_activa:.3f} kWh" if medicion.energia_activa else "0.000 kWh",
        timestamp=medicion.timestamp
    )


@router.get("/{device_id}/historico", response_model=MedicionHistorico)
def obtener_historico(device_id: str, horas: int = 24, limite: int = 500, db: Session = Depends(get_db)):
    device_id = device_id.strip().upper()
    ahora = datetime.utcnow()
    desde = ahora - timedelta(hours=horas)
    mediciones = db.query(Medicion).filter(Medicion.device_id == device_id, Medicion.timestamp >= desde).order_by(desc(Medicion.timestamp)).limit(limite).all()
    if not mediciones:
        raise HTTPException(status_code=404, detail=f"No hay mediciones para '{device_id}'")
    stats = db.query(
        func.avg(Medicion.voltaje_rms).label('v_avg'), func.min(Medicion.voltaje_rms).label('v_min'),
        func.max(Medicion.voltaje_rms).label('v_max'), func.avg(Medicion.potencia_activa).label('p_avg'),
        func.max(Medicion.potencia_activa).label('p_max')
    ).filter(Medicion.device_id == device_id, Medicion.timestamp >= desde).first()
    return MedicionHistorico(
        device_id=device_id, periodo_inicio=desde, periodo_fin=ahora, total_mediciones=len(mediciones),
        estadisticas={"voltaje_promedio": round(stats.v_avg or 0, 2), "voltaje_minimo": round(stats.v_min or 0, 2),
                      "voltaje_maximo": round(stats.v_max or 0, 2), "potencia_promedio": round(stats.p_avg or 0, 2),
                      "potencia_maxima": round(stats.p_max or 0, 2)},
        mediciones=mediciones
    )


@router.get("/{device_id}/reconciliacion")
def obtener_reconciliacion(
    device_id: str,
    horas: int = Query(default=24, ge=1, le=168, description="Ventana de tiempo (1-168h)"),
    db: Session = Depends(get_db),
):
    """
    Reconciliación de energía: compara la energía acumulada reportada por
    el dispositivo (energia_activa, valor del NVS del firmware) contra
    la integral de potencia calculada por la plataforma (∑ P·Δt).

    Devuelve el delta en Wh y el porcentaje de discrepancia para detectar
    pérdidas de paquetes, errores de integración o desvíos de calibración.
    """
    device_id = device_id.strip().upper()
    desde = datetime.now(timezone.utc) - timedelta(hours=horas)

    mediciones = (
        db.query(Medicion)
        .filter(Medicion.device_id == device_id, Medicion.timestamp >= desde)
        .order_by(asc(Medicion.timestamp))
        .all()
    )

    if len(mediciones) < 2:
        raise HTTPException(
            status_code=404,
            detail=f"Se necesitan al menos 2 mediciones para '{device_id}' en las últimas {horas}h",
        )

    primera = mediciones[0]
    ultima = mediciones[-1]

    # Energía según el dispositivo (NVS counter acumulado): diferencia entre
    # la primera y la última medición de la ventana. Si el contador cayó
    # (reinicio del firmware) se detecta y se reporta como reset_detectado.
    e_disp_inicio = primera.energia_activa or 0.0
    e_disp_fin = ultima.energia_activa or 0.0
    reset_detectado = e_disp_fin < e_disp_inicio
    energia_dispositivo_wh = (e_disp_fin - e_disp_inicio) if not reset_detectado else None

    # Integral de potencia: ∑ P(t) × Δt (regla del rectángulo izquierdo)
    energia_plataforma_wh = 0.0
    for i in range(1, len(mediciones)):
        t1 = mediciones[i - 1].timestamp
        t2 = mediciones[i].timestamp
        p_w = mediciones[i - 1].potencia_activa or 0.0
        dt_h = (t2 - t1).total_seconds() / 3600.0
        energia_plataforma_wh += p_w * dt_h

    energia_plataforma_wh = round(energia_plataforma_wh, 4)

    discrepancia_wh = None
    discrepancia_pct = None
    if energia_dispositivo_wh is not None and energia_dispositivo_wh > 0:
        discrepancia_wh = round(energia_dispositivo_wh - energia_plataforma_wh, 4)
        discrepancia_pct = round(abs(discrepancia_wh) / energia_dispositivo_wh * 100, 2)

    return {
        "device_id": device_id,
        "ventana_horas": horas,
        "total_mediciones": len(mediciones),
        "periodo_inicio": primera.timestamp,
        "periodo_fin": ultima.timestamp,
        "energia_dispositivo_wh": round(energia_dispositivo_wh, 4) if energia_dispositivo_wh is not None else None,
        "energia_plataforma_wh": energia_plataforma_wh,
        "discrepancia_wh": discrepancia_wh,
        "discrepancia_pct": discrepancia_pct,
        "reset_detectado": reset_detectado,
        "nota": (
            "Reset de firmware detectado en la ventana — energía dispositivo no disponible"
            if reset_detectado else
            f"Intervalo promedio: {round((ultima.timestamp - primera.timestamp).total_seconds() / max(len(mediciones)-1,1), 1)}s"
        ),
    }
