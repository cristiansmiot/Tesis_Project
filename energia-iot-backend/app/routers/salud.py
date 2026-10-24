"""Salud - Métricas de transmisión y salud del nodo ESP32"""
from fastapi import APIRouter, Depends, HTTPException, Query
from sqlalchemy.orm import Session
from sqlalchemy import func, desc
from datetime import datetime, timedelta, timezone
from typing import Optional

from app.database import get_db
from app.models.nodo_salud import NodoSalud
from app.models.medicion import Medicion
from app.models.evento import Evento
from app.services.telemetry_contract import sequence_metrics

router = APIRouter(prefix="/salud", tags=["Salud del Nodo"])


def _numeric_stats(values):
    values = sorted(float(v) for v in values if v is not None)
    if not values:
        return None

    def percentile(p):
        if len(values) == 1:
            return values[0]
        pos = (len(values) - 1) * p
        lo, hi = int(pos), min(int(pos) + 1, len(values) - 1)
        return values[lo] + (values[hi] - values[lo]) * (pos - lo)

    return {
        "min": round(values[0], 3),
        "promedio": round(sum(values) / len(values), 3),
        "p50": round(percentile(0.50), 3),
        "p95": round(percentile(0.95), 3),
        "p99": round(percentile(0.99), 3),
        "max": round(values[-1], 3),
        "muestras": len(values),
    }


def _sum_boot_max(rows, field):
    maxima = {}
    for row in rows:
        boot_id, value = row.boot_id, getattr(row, field, None)
        if boot_id is None or value is None:
            continue
        maxima[boot_id] = max(maxima.get(boot_id, 0), int(value))
    return sum(maxima.values()) if maxima else None


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
    - **PDR observado**: identidades recibidas / rango de secuencias por boot
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
    # Use measurement identities, never session counters or nominal row counts.
    mediciones = (
        db.query(Medicion)
        .filter(Medicion.device_id == device_id, Medicion.timestamp >= desde)
        .all()
    )
    pdr = sequence_metrics(mediciones)

    # ── Reconexiones ────────────────────────────────────────────────────
    # Tomar el primer y último registro para calcular incremento
    primer_reg = registros[0]
    ultimo_reg = registros[-1]

    reconexiones_delta = None
    if (primer_reg.boot_id == ultimo_reg.boot_id and
            primer_reg.reconexiones is not None and ultimo_reg.reconexiones is not None and
            ultimo_reg.reconexiones >= primer_reg.reconexiones):
        reconexiones_delta = ultimo_reg.reconexiones - primer_reg.reconexiones

    # ── msg_tx crecimiento ──────────────────────────────────────────────
    msg_tx_delta = None
    if (primer_reg.boot_id == ultimo_reg.boot_id and
            primer_reg.msg_tx is not None and ultimo_reg.msg_tx is not None and
            ultimo_reg.msg_tx >= primer_reg.msg_tx):
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
        "radio_valid": ultimo.radio_valid,
        "rsrp_dbm": ultimo.rsrp_dbm,
        "rsrq_db": ultimo.rsrq_db,
        "rssnr_raw": ultimo.rssnr_raw,
        "sinr_db": ultimo.sinr_db,
        "boot_id": ultimo.boot_id,
        "uptime_ms": ultimo.uptime_ms,
        "ts_device": ultimo.ts_device,
        "time_valid": ultimo.time_valid,
        "data_generated_count": ultimo.data_generated_count,
        "data_publish_attempts": ultimo.data_publish_attempts,
        "data_publish_ok": ultimo.data_publish_ok,
        "ac_loss_count": ultimo.ac_loss_count,
        "ac_restore_count": ultimo.ac_restore_count,
        "reset_reason": ultimo.reset_reason,
        "reset_reason_name": ultimo.reset_reason_name,
        "msg_tx": ultimo.msg_tx,
        "reconexiones": ultimo.reconexiones,
        "red_intentos": ultimo.red_intentos,
        "red_exitos": ultimo.red_exitos,
        "mqtt_intentos": ultimo.mqtt_intentos,
        "mqtt_exitos": ultimo.mqtt_exitos,
        "fw_version": ultimo.fw_version,
        "imei":       ultimo.imei,
    }

    return {
        "device_id": device_id,
        "ventana_horas": horas,
        "registros_salud": len(registros),
        "pdr": pdr,
        "radio": {"historico": [{"ts": r.timestamp, "boot_id": r.boot_id,
            "radio_valid": r.radio_valid, "rsrp_dbm": r.rsrp_dbm,
            "rsrq_db": r.rsrq_db, "rssnr_raw": r.rssnr_raw, "sinr_db": r.sinr_db}
            for r in registros]},
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
                "radio_valid": r.radio_valid,
                "rsrp_dbm": r.rsrp_dbm,
                "rsrq_db": r.rsrq_db,
                "rssnr_raw": r.rssnr_raw,
                "sinr_db": r.sinr_db,
                "boot_id": r.boot_id,
                "uptime_ms": r.uptime_ms,
                "ts_device": r.ts_device,
                "time_valid": r.time_valid,
                "data_generated_count": r.data_generated_count,
                "data_publish_attempts": r.data_publish_attempts,
                "data_publish_ok": r.data_publish_ok,
                "ac_loss_count": r.ac_loss_count,
                "ac_restore_count": r.ac_restore_count,
                "reset_reason": r.reset_reason,
                "reset_reason_name": r.reset_reason_name,
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


@router.get("/{device_id}/estabilidad")
def obtener_estabilidad(
    device_id: str,
    desde: Optional[datetime] = Query(None, description="Inicio UTC inclusivo"),
    hasta: Optional[datetime] = Query(None, description="Fin UTC inclusivo"),
    horas: int = Query(default=12, ge=1, le=168),
    db: Session = Depends(get_db),
):
    """Resumen reproducible para una corrida de estabilidad/Objetivo 4."""
    device_id = device_id.strip().upper()
    hasta = hasta or datetime.now(timezone.utc)
    if hasta.tzinfo is None:
        hasta = hasta.replace(tzinfo=timezone.utc)
    desde = desde or (hasta - timedelta(hours=horas))
    if desde.tzinfo is None:
        desde = desde.replace(tzinfo=timezone.utc)
    if desde >= hasta:
        raise HTTPException(status_code=400, detail="desde debe ser anterior a hasta")

    mediciones = (db.query(Medicion)
        .filter(Medicion.device_id == device_id,
                Medicion.timestamp >= desde, Medicion.timestamp <= hasta)
        .order_by(Medicion.timestamp.asc()).all())
    salud = (db.query(NodoSalud)
        .filter(NodoSalud.device_id == device_id,
                NodoSalud.timestamp >= desde, NodoSalud.timestamp <= hasta)
        .order_by(NodoSalud.timestamp.asc()).all())
    eventos = (db.query(Evento)
        .filter(Evento.device_id == device_id,
                Evento.timestamp >= desde, Evento.timestamp <= hasta)
        .order_by(Evento.timestamp.asc()).all())
    if not mediciones and not salud:
        raise HTTPException(status_code=404, detail="Sin datos en la ventana solicitada")

    pdr = sequence_metrics(mediciones)
    boots = sorted({r.boot_id for r in mediciones + salud if r.boot_id is not None})
    latency_ms = [
        (r.ts_server - r.ts_device).total_seconds() * 1000.0
        for r in mediciones if r.ts_server is not None and r.ts_device is not None
        and (r.ts_server - r.ts_device).total_seconds() >= 0
    ]
    tipos = {}
    for event in eventos:
        tipos[event.tipo] = tipos.get(event.tipo, 0) + 1
    reset_reasons = []
    reset_boots = set()
    for row in salud:
        if (row.boot_id is not None and row.reset_reason is not None and
                row.boot_id not in reset_boots):
            reset_boots.add(row.boot_id)
            reset_reasons.append({"boot_id": row.boot_id,
                                  "reason": row.reset_reason_name,
                                  "code": row.reset_reason})

    def status_pct(field):
        values = [getattr(r, field) for r in salud if getattr(r, field) is not None]
        return round(100.0 * sum(bool(v) for v in values) / len(values), 2) if values else None

    return {
        "device_id": device_id,
        "ventana": {"desde": desde, "hasta": hasta,
                    "duracion_horas": round((hasta - desde).total_seconds() / 3600, 3)},
        "continuidad": {
            "boot_ids": boots,
            "cantidad_boots": len(boots),
            "reinicios_detectados": max(0, len(boots) - 1),
            "reset_reasons": reset_reasons,
        },
        "paquetes": {
            "generados_dispositivo": _sum_boot_max(salud, "data_generated_count"),
            "intentos_publicacion": _sum_boot_max(salud, "data_publish_attempts"),
            "aceptados_por_modem": _sum_boot_max(salud, "data_publish_ok"),
            "recibidos_unicos_backend": pdr["mediciones_recibidas"],
            "pdr_observado": pdr,
        },
        "latencia_ms": _numeric_stats(latency_ms),
        "electricas": {
            "voltaje_v": _numeric_stats([r.voltaje_rms for r in mediciones]),
            "frecuencia_hz": _numeric_stats([r.frecuencia for r in mediciones]),
            "corriente_a": _numeric_stats([r.corriente_rms for r in mediciones]),
            "potencia_w": _numeric_stats([r.potencia_activa for r in mediciones]),
            "ac_perdidas_contador": _sum_boot_max(salud, "ac_loss_count"),
            "ac_restauraciones_contador": _sum_boot_max(salud, "ac_restore_count"),
            "ac_ok_porcentaje_muestras_estado": status_pct("ac_ok"),
        },
        "comunicaciones": {
            "reconexiones_mqtt": _sum_boot_max(salud, "reconexiones"),
            "intentos_mqtt": _sum_boot_max(salud, "mqtt_intentos"),
            "sesiones_mqtt_exitosas": _sum_boot_max(salud, "mqtt_exitos"),
            "intentos_celular": _sum_boot_max(salud, "red_intentos"),
            "conexiones_celular_exitosas": _sum_boot_max(salud, "red_exitos"),
            "mqtt_ok_porcentaje_muestras_estado": status_pct("mqtt_ok"),
            "modem_ok_porcentaje_muestras_estado": status_pct("modem_ok"),
            "rsrp_dbm": _numeric_stats([r.rsrp_dbm for r in salud]),
            "rsrq_db": _numeric_stats([r.rsrq_db for r in salud]),
            "sinr_db": _numeric_stats([r.sinr_db for r in salud]),
        },
        "ade": {
            "disponibilidad_porcentaje_muestras_estado": status_pct("ade_ok"),
            "perdidas": _sum_boot_max(salud, "ade_perdidas"),
            "recuperaciones": _sum_boot_max(salud, "ade_rec"),
        },
        "alarmas": {
            "total": len(eventos),
            "activas_al_cierre": sum(1 for e in eventos if e.activo),
            "por_tipo": tipos,
        },
        "limitacion": "Sin microSD, el PDR solo cubre huecos entre la primera y ultima secuencia recibidas por boot.",
    }
