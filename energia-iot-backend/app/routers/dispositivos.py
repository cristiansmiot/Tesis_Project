"""ROUTER: Dispositivos - CRUD de medidores de energía"""
from fastapi import APIRouter, Body, Depends, HTTPException, Query, status
from sqlalchemy.orm import Session
from typing import List, Optional
from datetime import datetime, timedelta, timezone

from app.database import get_db
from app.models.dispositivo import Dispositivo
from app.models.nodo_salud import NodoSalud
from app.schemas.dispositivo import DispositivoCreate, DispositivoUpdate, DispositivoResponse, DispositivoEstado
from app.services.mqtt_client import get_mqtt_client

# Timeout para considerar dispositivo desconectado (segundos).
# Si no se recibe mensaje en este periodo, se marca offline automaticamente.
DEVICE_OFFLINE_TIMEOUT_S = 180  # 3 minutos (3x intervalo de publicacion de 60s)

router = APIRouter(prefix="/dispositivos", tags=["Dispositivos"])


@router.get("/", response_model=List[DispositivoResponse])
def listar_dispositivos(skip: int = 0, limit: int = 100, activo: Optional[bool] = None, db: Session = Depends(get_db)):
    query = db.query(Dispositivo)
    if activo is not None:
        query = query.filter(Dispositivo.activo == activo)
    return query.offset(skip).limit(limit).all()


@router.get("/{device_id}", response_model=DispositivoResponse)
def obtener_dispositivo(device_id: str, db: Session = Depends(get_db)):
    device_id = device_id.strip().upper()
    dispositivo = db.query(Dispositivo).filter(Dispositivo.device_id == device_id).first()
    if not dispositivo:
        raise HTTPException(status_code=404, detail=f"Dispositivo '{device_id}' no encontrado")
    return dispositivo


@router.post("/", response_model=DispositivoResponse, status_code=status.HTTP_201_CREATED)
def crear_dispositivo(data: DispositivoCreate, db: Session = Depends(get_db)):
    existente = db.query(Dispositivo).filter(Dispositivo.device_id == data.device_id).first()
    if existente:
        raise HTTPException(status_code=400, detail=f"Ya existe dispositivo '{data.device_id}'")
    nuevo = Dispositivo(device_id=data.device_id, nombre=data.nombre, ubicacion=data.ubicacion,
                        descripcion=data.descripcion, intervalo_medicion=data.intervalo_medicion)
    db.add(nuevo)
    db.commit()
    db.refresh(nuevo)
    return nuevo


@router.patch("/{device_id}", response_model=DispositivoResponse)
def actualizar_dispositivo(device_id: str, data: DispositivoUpdate, db: Session = Depends(get_db)):
    device_id = device_id.strip().upper()
    dispositivo = db.query(Dispositivo).filter(Dispositivo.device_id == device_id).first()
    if not dispositivo:
        raise HTTPException(status_code=404, detail=f"Dispositivo '{device_id}' no encontrado")
    for campo, valor in data.model_dump(exclude_unset=True).items():
        setattr(dispositivo, campo, valor)
    db.commit()
    db.refresh(dispositivo)
    return dispositivo


@router.delete("/{device_id}", status_code=status.HTTP_204_NO_CONTENT)
def eliminar_dispositivo(device_id: str, db: Session = Depends(get_db)):
    device_id = device_id.strip().upper()
    dispositivo = db.query(Dispositivo).filter(Dispositivo.device_id == device_id).first()
    if not dispositivo:
        raise HTTPException(status_code=404, detail=f"Dispositivo '{device_id}' no encontrado")
    db.delete(dispositivo)
    db.commit()


@router.get("/{device_id}/estado", response_model=DispositivoEstado)
def obtener_estado(device_id: str, db: Session = Depends(get_db)):
    device_id = device_id.strip().upper()
    dispositivo = db.query(Dispositivo).filter(Dispositivo.device_id == device_id).first()
    if not dispositivo:
        raise HTTPException(status_code=404, detail=f"Dispositivo '{device_id}' no encontrado")

    # Deteccion de desconexion por timeout: si no hay mensaje reciente,
    # marcar offline independientemente del flag LWT.
    conectado = dispositivo.conectado
    tiempo_sin_conexion = None
    if dispositivo.ultima_conexion:
        now_utc = datetime.now(timezone.utc)
        ult = dispositivo.ultima_conexion
        if ult.tzinfo is None:
            ult = ult.replace(tzinfo=timezone.utc)
        delta = now_utc - ult
        segundos = int(delta.total_seconds())

        # Timeout-based offline: si supera el umbral, forzar desconectado
        if segundos > DEVICE_OFFLINE_TIMEOUT_S:
            conectado = False

        if segundos < 60:
            tiempo_sin_conexion = f"{segundos} segundos"
        elif segundos < 3600:
            tiempo_sin_conexion = f"{segundos // 60} minutos"
        else:
            tiempo_sin_conexion = f"{segundos // 3600} horas"
    else:
        # Sin ultima_conexion registrada → nunca se conecto
        conectado = False

    return DispositivoEstado(device_id=dispositivo.device_id, nombre=dispositivo.nombre or "Sin nombre",
                             conectado=conectado, ultima_conexion=dispositivo.ultima_conexion,
                             tiempo_sin_conexion=tiempo_sin_conexion)


@router.get("/{device_id}/salud")
def obtener_salud(device_id: str, db: Session = Depends(get_db)):
    """
    Retorna el último registro de salud del nodo (topic /estado).
    Incluye indicadores binarios (ac_ok, ade_ok, cal_ok, modem_ok) y
    métricas de transmisión (rssi_dbm, msg_tx, reconexiones).
    """
    device_id = device_id.strip().upper()
    registro = (
        db.query(NodoSalud)
        .filter(NodoSalud.device_id == device_id)
        .order_by(NodoSalud.timestamp.desc())
        .first()
    )
    if not registro:
        raise HTTPException(status_code=404, detail=f"Sin datos de salud para '{device_id}'")
    return {
        "device_id":    registro.device_id,
        "timestamp":    registro.timestamp,
        "online":       registro.online,
        "ac_ok":        registro.ac_ok,
        "carga":        registro.carga,
        "ade_ok":       registro.ade_ok,
        "ade_bus":      registro.ade_bus,
        "ade_rec":      registro.ade_rec,
        "modem_ok":     registro.modem_ok,
        "mqtt_ok":      registro.mqtt_ok,
        "cal_ok":       registro.cal_ok,
        "rssi_dbm":     registro.rssi_dbm,
        "msg_tx":       registro.msg_tx,
        "reconexiones": registro.reconexiones,
        "fw_version":   registro.fw_version,
    }


# Comandos remotos validos que el ESP32 acepta
VALID_COMMANDS = {"calibrate", "probe", "reset"}


@router.post("/{device_id}/comando")
def enviar_comando(
    device_id: str,
    comando: str = Body(..., embed=True),
    db: Session = Depends(get_db),
):
    """
    Envia un comando remoto al dispositivo via MQTT.

    Comandos soportados:
      - calibrate: inicia calibracion mSure del ADE9153A
      - probe: verifica comunicacion SPI con el ADE9153A
      - reset: reinicia el ESP32

    El comando se publica en el topic medidor/{device_id}/cmd
    y el firmware lo recibe via AT+SMSUB URC.
    """
    device_id = device_id.strip().upper()

    # Validar que el dispositivo existe
    dispositivo = db.query(Dispositivo).filter(
        Dispositivo.device_id == device_id
    ).first()
    if not dispositivo:
        raise HTTPException(status_code=404, detail=f"Dispositivo '{device_id}' no encontrado")

    # Validar comando
    comando = comando.strip().lower()
    if comando not in VALID_COMMANDS:
        raise HTTPException(
            status_code=400,
            detail=f"Comando '{comando}' no valido. Comandos soportados: {', '.join(sorted(VALID_COMMANDS))}",
        )

    # Publicar via MQTT
    client = get_mqtt_client()
    if not client or not client.is_connected:
        raise HTTPException(status_code=503, detail="Cliente MQTT no conectado al broker")

    topic = f"medidor/{device_id}/cmd"
    try:
        client.publish(topic, comando, qos=1)
    except RuntimeError as e:
        raise HTTPException(status_code=503, detail=str(e))

    return {
        "status": "ok",
        "device_id": device_id,
        "comando": comando,
        "topic": topic,
        "mensaje": f"Comando '{comando}' enviado a {device_id}",
    }
