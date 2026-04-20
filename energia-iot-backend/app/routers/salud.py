"""ROUTER: Salud - Métricas de transmisión y salud del nodo ESP32"""
from fastapi import APIRouter, Depends, HTTPException, Query
from sqlalchemy.orm import Session
from sqlalchemy import func, desc
from datetime import datetime, timedelta, timezone
from typing import Optional

from app.database import get_db
from app.models.nodo_salud import NodoSalud
from app.models.medicion import Medicion

router = APIRouter(prefix="/salud", tags=["Salud del Nodo"])


@router.get("/{device_id}/metricas")
def obtener_metricas(
    device_id: str,
    horas: int = Query(default=24, ge=1, le=168, description="Ventana de tiempo en horas (1-168)"),
    db: Session = Depends(get_db),
):
    """
    Retorna métricas de transmisión históricas del nodo.

    Incluye:
    - **RSSI histórico**: lista de (timestamp, rssi_dbm) para gráfico de señal
    - **PDR estimado**: Packet Delivery Rate = mediciones recibidas / mediciones esperadas
    - **Resumen de reconexiones**: total y tendencia
    - **msg_tx**: crecimiento del contador de mensajes
    - **Indicadores**: último estado de salud conocido
    """
    device_id = device_id.strip().upper()
    desde = datetime.now(timezone.utc) - timedelta(hours=horas)

    # ── Registros de salud en la ventana ────────────────────────────────
    registros = (
        db.query(NodoSalud)
        .filter(NodoSalud.device_id == device_id, NodoSalud.timestamp >= desde)
        .order_by(NodoSalud.timestamp.asc())
        .all()
    )

    if not registros:
        raise HTTPException(
            status_code=404,
            detail=f"Sin datos de salud para '{device_id}' en las últimas {horas}h",
        )

    # ── RSSI histórico ──────────────────────────────────────────────────
    rssi_historico = [
        {"ts": r.timestamp.isoformat(), "rssi_dbm": r.rssi_dbm}
        for r in registros
        if r.rssi_dbm is not None and r.rssi_dbm > -127
    ]

    # ── Estadísticas de RSSI ────────────────────────────────────────────
    rssi_values = [r.rssi_dbm for r in registros if r.rssi_dbm is not None and r.rssi_dbm > -127]
    rssi_stats = None
    if rssi_values:
        rssi_stats = {
            "min_dbm": min(rssi_values),
            "max_dbm": max(rssi_values),
            "avg_dbm": round(sum(rssi_values) / len(rssi_values), 1),
            "muestras": len(rssi_values),
        }

    # ── PDR (Packet Delivery Rate) ──────────────────────────────────────
    # Mediciones recibidas en la ventana (tabla mediciones)
    mediciones_recibidas = (
        db.query(func.count(Medicion.id))
        .filter(Medicion.device_id == device_id, Medicion.timestamp >= desde)
        .scalar()
    ) or 0

    # Mediciones esperadas = ventana_segundos / intervalo_publicacion_segundos
    # El firmware publica /datos cada METER_COMM_PUBLISH_PERIOD_MS = 60s
    intervalo_s = 60
    ventana_s = horas * 3600
    mediciones_esperadas = ventana_s // intervalo_s

    pdr = None
    if mediciones_esperadas > 0:
        pdr = round(min(mediciones_recibidas / mediciones_esperadas, 1.0) * 100, 2)

    # ── Reconexiones ────────────────────────────────────────────────────
    # Tomar el primer y último registro para calcular incremento
    primer_reg = registros[0]
    ultimo_reg = registros[-1]

    reconexiones_delta = None
    if primer_reg.reconexiones is not None and ultimo_reg.reconexiones is not None:
        reconexiones_delta = ultimo_reg.reconexiones - primer_reg.reconexiones

    # ── msg_tx crecimiento ──────────────────────────────────────────────
    msg_tx_delta = None
    if primer_reg.msg_tx is not None and ultimo_reg.msg_tx is not None:
        msg_tx_delta = ultimo_reg.msg_tx - primer_reg.msg_tx

    # ── msg_tx histórico (para gráfico) ─────────────────────────────────
    msg_tx_historico = [
        {"ts": r.timestamp.isoformat(), "msg_tx": r.msg_tx}
        for r in registros
        if r.msg_tx is not None
    ]

    # ── Último estado conocido ──────────────────────────────────────────
    ultimo = ultimo_reg
    ultimo_estado = {
        "timestamp": ultimo.timestamp.isoformat(),
        "online": ultimo.online,
        "ac_ok": ultimo.ac_ok,
        "carga": ultimo.carga,
        "ade_ok": ultimo.ade_ok,
        "ade_bus": ultimo.ade_bus,
        "ade_rec": ultimo.ade_rec,
        "ade_perdidas": ultimo.ade_perdidas,
        "modem_ok": ultimo.modem_ok,
        "mqtt_ok": ultimo.mqtt_ok,
        "cal_ok": ultimo.cal_ok,
        "rssi_dbm": ultimo.rssi_dbm,
        "msg_tx": ultimo.msg_tx,
        "reconexiones": ultimo.reconexiones,
        "red_intentos": ultimo.red_intentos,
        "red_exitos": ultimo.red_exitos,
        "mqtt_intentos": ultimo.mqtt_intentos,
        "mqtt_exitos": ultimo.mqtt_exitos,
        "fw_version": ultimo.fw_version,
    }

    return {
        "device_id": device_id,
        "ventana_horas": horas,
        "registros_salud": len(registros),
        "pdr": {
            "porcentaje": pdr,
            "mediciones_recibidas": mediciones_recibidas,
            "mediciones_esperadas": mediciones_esperadas,
            "intervalo_publish_s": intervalo_s,
        },
        "rssi": {
            "stats": rssi_stats,
            "historico": rssi_historico,
        },
        "reconexiones": {
            "total_actual": ultimo_reg.reconexiones,
            "delta_ventana": reconexiones_delta,
        },
        "msg_tx": {
            "total_actual": ultimo_reg.msg_tx,
            "delta_ventana": msg_tx_delta,
            "historico": msg_tx_historico,
        },
        "ultimo_estado": ultimo_estado,
    }


@router.get("/{device_id}/historico")
def obtener_historico_salud(
    device_id: str,
    horas: int = Query(default=24, ge=1, le=168),
    limit: int = Query(default=100, ge=1, le=500),
    db: Session = Depends(get_db),
):
    """
    Retorna el historial de registros de salud del nodo (paginado).
    Útil para debug y análisis detallado.
    """
    device_id = device_id.strip().upper()
    desde = datetime.now(timezone.utc) - timedelta(hours=horas)

    registros = (
        db.query(NodoSalud)
        .filter(NodoSalud.device_id == device_id, NodoSalud.timestamp >= desde)
        .order_by(desc(NodoSalud.timestamp))
        .limit(limit)
        .all()
    )

    return {
        "device_id": device_id,
        "total": len(registros),
        "registros": [
            {
                "timestamp": r.timestamp,
                "online": r.online,
                "ac_ok": r.ac_ok,
                "carga": r.carga,
                "ade_ok": r.ade_ok,
                "ade_bus": r.ade_bus,
                "ade_rec": r.ade_rec,
                "ade_perdidas": r.ade_perdidas,
                "modem_ok": r.modem_ok,
                "mqtt_ok": r.mqtt_ok,
                "cal_ok": r.cal_ok,
                "rssi_dbm": r.rssi_dbm,
                "msg_tx": r.msg_tx,
                "reconexiones": r.reconexiones,
                "red_intentos": r.red_intentos,
                "red_exitos": r.red_exitos,
                "mqtt_intentos": r.mqtt_intentos,
                "mqtt_exitos": r.mqtt_exitos,
                "fw_version": r.fw_version,
            }
            for r in registros
        ],
    }
