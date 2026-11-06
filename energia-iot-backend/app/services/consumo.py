"""Consumo de energía a partir del contador acumulado del medidor.

El firmware publica ``e_act`` como contador acumulado en Wh (persistido en la
NVS del medidor). El consumo entre dos lecturas consecutivas de un mismo
medidor es la diferencia del contador:

- una diferencia negativa indica que el contador se reinició y no se cuenta;
- la primera lectura de una ventana usa como base la última lectura anterior
  solo si no tiene más de ``BASE_MAX_ANTIGUEDAD``. Así el consumo de un corte
  largo no se atribuye a la ventana.

Los intervalos se alinean a la hora local de Bogotá (UTC-5, sin horario de
verano), que es la de la factura en kWh (Resolución CREG 024 de 2015). Es el
mismo criterio de ``energia-iot-frontend/src/utils/consumo.js``.
"""
from dataclasses import dataclass
from datetime import datetime, timedelta, timezone
from typing import Dict, List, Optional

from sqlalchemy.orm import Session

from app.models.medicion import Medicion

BOGOTA = timezone(timedelta(hours=-5))
HORA = timedelta(hours=1)
BASE_MAX_ANTIGUEDAD = timedelta(hours=1)


@dataclass(frozen=True)
class Lectura:
    """Una lectura del contador dentro de la ventana consultada."""

    device_id: str
    timestamp: datetime
    wh: float  # consumo desde la lectura anterior del mismo medidor
    hueco: bool  # la lectura anterior tiene más de BASE_MAX_ANTIGUEDAD


def a_utc(ts: datetime) -> datetime:
    """SQLite devuelve las fechas sin zona; en la base se guardan en UTC."""
    if ts.tzinfo is None:
        return ts.replace(tzinfo=timezone.utc)
    return ts.astimezone(timezone.utc)


def inicio_hora_local(ts: datetime) -> datetime:
    """Inicio (UTC) de la hora local de Bogotá que contiene ``ts``."""
    local = a_utc(ts).astimezone(BOGOTA)
    return local.replace(minute=0, second=0, microsecond=0).astimezone(timezone.utc)


def inicio_dia_local(ts: datetime) -> datetime:
    """Inicio (UTC) del día local de Bogotá que contiene ``ts``."""
    local = a_utc(ts).astimezone(BOGOTA)
    return local.replace(hour=0, minute=0, second=0, microsecond=0).astimezone(timezone.utc)


def lecturas_ventana(db: Session, desde: datetime, hasta: datetime) -> List[Lectura]:
    """Lecturas con marca de tiempo en (desde, hasta] y su consumo en Wh."""
    desde = a_utc(desde)
    hasta = a_utc(hasta)
    filas = (
        db.query(Medicion.device_id, Medicion.timestamp, Medicion.energia_activa)
        .filter(
            Medicion.timestamp > desde - BASE_MAX_ANTIGUEDAD,
            Medicion.timestamp <= hasta,
            Medicion.energia_activa.isnot(None),
        )
        .order_by(Medicion.device_id, Medicion.timestamp, Medicion.id)
        .all()
    )

    lecturas: List[Lectura] = []
    previa: Dict[str, tuple] = {}
    for device_id, ts, energia in filas:
        ts = a_utc(ts)
        anterior = previa.get(device_id)
        previa[device_id] = (ts, energia)
        if ts <= desde:
            continue  # solo sirve de base para la primera lectura de la ventana
        if anterior is None:
            lecturas.append(Lectura(device_id, ts, 0.0, False))
            continue
        delta = energia - anterior[1]
        lecturas.append(Lectura(
            device_id,
            ts,
            delta if delta > 0 else 0.0,
            ts - anterior[0] > BASE_MAX_ANTIGUEDAD,
        ))
    return lecturas


def consumo_wh(db: Session, desde: datetime, hasta: datetime) -> float:
    """Consumo total (Wh) de todos los medidores en (desde, hasta]."""
    return sum(l.wh for l in lecturas_ventana(db, desde, hasta))


def consumo_por_hora(db: Session, horas: int, ahora: Optional[datetime] = None) -> List[dict]:
    """Consumo por hora local, de la hora ``horas - 1`` atrás hasta la actual.

    Cada elemento: inicio (UTC), wh, muestras (lecturas en esa hora) y
    con_hueco (alguna lectura cierra un periodo sin datos de más de una hora,
    así que su barra incluye consumo de horas anteriores).
    """
    ahora = a_utc(ahora or datetime.now(timezone.utc))
    fin = inicio_hora_local(ahora) + HORA
    inicio = fin - horas * HORA
    cubetas = {
        inicio + i * HORA: {"inicio": inicio + i * HORA, "wh": 0.0, "muestras": 0, "con_hueco": False}
        for i in range(horas)
    }
    for lectura in lecturas_ventana(db, inicio, ahora):
        cubeta = cubetas.get(inicio_hora_local(lectura.timestamp))
        if cubeta is None:
            continue
        cubeta["wh"] += lectura.wh
        cubeta["muestras"] += 1
        cubeta["con_hueco"] = cubeta["con_hueco"] or lectura.hueco
    return [cubetas[k] for k in sorted(cubetas)]
