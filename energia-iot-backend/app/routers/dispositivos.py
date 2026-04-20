"""
ROUTER: Dispositivos - CRUD de medidores de energia

Endpoints para listar, crear, actualizar y eliminar dispositivos (medidores).
Control de acceso:
  - GET (listar/obtener/estado/salud): Todos los roles autenticados.
    El visualizador solo ve los dispositivos que tiene asignados.
  - POST (crear): Solo super_admin y operador.
  - PATCH (actualizar): Solo super_admin y operador.
  - DELETE (eliminar): Solo super_admin.
"""
from fastapi import APIRouter, Depends, HTTPException, Query, status
from sqlalchemy.orm import Session
from typing import List, Optional
from datetime import datetime, timedelta

from app.database import get_db
from app.models.dispositivo import Dispositivo
from app.models.nodo_salud import NodoSalud
from app.models.usuario import Usuario
from app.schemas.dispositivo import DispositivoCreate, DispositivoUpdate, DispositivoResponse, DispositivoEstado
from app.services.auth import (
    get_current_user, get_optional_user, obtener_dispositivos_asignados,
    require_operador_or_admin, require_super_admin,
)

router = APIRouter(prefix="/dispositivos", tags=["Dispositivos"])


def _verificar_acceso_dispositivo(device_id: str, usuario: Optional[Usuario], db: Session):
    """
    Verifica que el usuario tenga acceso al dispositivo solicitado.
    - super_admin y operador: acceso a todos los dispositivos.
    - visualizador: solo puede acceder a los dispositivos que tiene asignados
      en la tabla usuario_dispositivos.
    - Sin autenticacion (usuario=None): se permite para endpoints publicos.
    """
    if usuario and usuario.es_visualizador:
        dispositivos_permitidos = obtener_dispositivos_asignados(usuario, db)
        if dispositivos_permitidos is not None and device_id not in dispositivos_permitidos:
            raise HTTPException(
                status_code=status.HTTP_403_FORBIDDEN,
                detail=f"No tienes acceso al dispositivo '{device_id}'",
            )


@router.get("/", response_model=List[DispositivoResponse])
def listar_dispositivos(
    skip: int = 0,
    limit: int = 100,
    activo: Optional[bool] = None,
    db: Session = Depends(get_db),
    usuario: Optional[Usuario] = Depends(get_optional_user),
):
    query = db.query(Dispositivo)
    if activo is not None:
        query = query.filter(Dispositivo.activo == activo)

    # Visualizador solo ve sus dispositivos asignados
    if usuario and usuario.es_visualizador:
        dispositivos_permitidos = obtener_dispositivos_asignados(usuario, db)
        if dispositivos_permitidos is not None:
            query = query.filter(Dispositivo.device_id.in_(dispositivos_permitidos))

    return query.offset(skip).limit(limit).all()


@router.get("/{device_id}", response_model=DispositivoResponse)
def obtener_dispositivo(
    device_id: str,
    db: Session = Depends(get_db),
    usuario: Optional[Usuario] = Depends(get_optional_user),
):
    device_id = device_id.strip().upper()
    _verificar_acceso_dispositivo(device_id, usuario, db)
    dispositivo = db.query(Dispositivo).filter(Dispositivo.device_id == device_id).first()
    if not dispositivo:
        raise HTTPException(status_code=404, detail=f"Dispositivo '{device_id}' no encontrado")
    return dispositivo


@router.post("/", response_model=DispositivoResponse, status_code=status.HTTP_201_CREATED)
def crear_dispositivo(
    data: DispositivoCreate,
    db: Session = Depends(get_db),
    _usuario: Usuario = Depends(require_operador_or_admin),
):
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
def actualizar_dispositivo(
    device_id: str,
    data: DispositivoUpdate,
    db: Session = Depends(get_db),
    _usuario: Usuario = Depends(require_operador_or_admin),
):
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
def eliminar_dispositivo(
    device_id: str,
    db: Session = Depends(get_db),
    _usuario: Usuario = Depends(require_super_admin),
):
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
    tiempo_sin_conexion = None
    if dispositivo.ultima_conexion:
        delta = datetime.utcnow() - dispositivo.ultima_conexion.replace(tzinfo=None)
        segundos = int(delta.total_seconds())
        if segundos < 60:
            tiempo_sin_conexion = f"{segundos} segundos"
        elif segundos < 3600:
            tiempo_sin_conexion = f"{segundos // 60} minutos"
        else:
            tiempo_sin_conexion = f"{segundos // 3600} horas"
    return DispositivoEstado(device_id=dispositivo.device_id, nombre=dispositivo.nombre or "Sin nombre",
                             conectado=dispositivo.conectado, ultima_conexion=dispositivo.ultima_conexion,
                             tiempo_sin_conexion=tiempo_sin_conexion)


@router.get("/{device_id}/salud")
def obtener_salud(device_id: str, db: Session = Depends(get_db)):
    """
    Retorna el último registro de salud del nodo (topic /estado).
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
        "device_id":     registro.device_id,
        "timestamp":     registro.timestamp,
        "online":        registro.online,
        "ac_ok":         registro.ac_ok,
        "carga":         registro.carga,
        "ade_ok":        registro.ade_ok,
        "ade_bus":       registro.ade_bus,
        "ade_rec":       registro.ade_rec,
        "ade_perdidas":  registro.ade_perdidas,
        "modem_ok":      registro.modem_ok,
        "mqtt_ok":       registro.mqtt_ok,
        "cal_ok":        registro.cal_ok,
        "rssi_dbm":      registro.rssi_dbm,
        "msg_tx":        registro.msg_tx,
        "reconexiones":  registro.reconexiones,
        "red_intentos":  registro.red_intentos,
        "red_exitos":    registro.red_exitos,
        "mqtt_intentos": registro.mqtt_intentos,
        "mqtt_exitos":   registro.mqtt_exitos,
        "fw_version":    registro.fw_version,
    }
