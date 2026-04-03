"""ROUTER: Comandos - Envío de comandos a dispositivos via MQTT"""
import json
import logging
from fastapi import APIRouter, Depends, HTTPException, Request
from pydantic import BaseModel
from sqlalchemy.orm import Session
from typing import Optional

from app.database import get_db
from app.models.dispositivo import Dispositivo
from app.models.usuario import Usuario
from app.services.auth import get_current_user
from app.services.audit import registrar_accion
from app.services.mqtt_client import get_mqtt_client

logger = logging.getLogger(__name__)

router = APIRouter(prefix="/dispositivos", tags=["Comandos"])


# ── Schemas ──────────────────────────────────────────────────────────────────

COMANDOS_VALIDOS = {
    "reiniciar": {
        "descripcion": "Reinicia el microcontrolador ESP32",
        "payload": {"cmd": "reboot"},
    },
    "corte_suministro": {
        "descripcion": "Abre el relé de corte de energía",
        "payload": {"cmd": "relay_off"},
    },
    "restaurar_suministro": {
        "descripcion": "Cierra el relé para restaurar energía",
        "payload": {"cmd": "relay_on"},
    },
    "sincronizar_hora": {
        "descripcion": "Sincroniza el reloj del dispositivo con el servidor",
        "payload": {"cmd": "sync_time"},
    },
    "solicitar_estado": {
        "descripcion": "Solicita que el dispositivo envíe su estado actual",
        "payload": {"cmd": "status"},
    },
}


class ComandoRequest(BaseModel):
    comando: str  # reiniciar, corte_suministro, restaurar_suministro, sincronizar_hora, solicitar_estado
    parametros: Optional[dict] = None


class ComandoResponse(BaseModel):
    exito: bool
    mensaje: str
    device_id: str
    comando: str
    topic: str


# ── Endpoints ────────────────────────────────────────────────────────────────

@router.get("/{device_id}/comandos/disponibles")
def listar_comandos_disponibles(device_id: str, db: Session = Depends(get_db)):
    """Listar comandos disponibles para un dispositivo."""
    dispositivo = db.query(Dispositivo).filter(Dispositivo.device_id == device_id).first()
    if not dispositivo:
        raise HTTPException(status_code=404, detail="Dispositivo no encontrado")

    return {
        "device_id": device_id,
        "comandos": {
            nombre: {"descripcion": info["descripcion"]}
            for nombre, info in COMANDOS_VALIDOS.items()
        },
    }


@router.post("/{device_id}/comando", response_model=ComandoResponse)
def enviar_comando(
    device_id: str,
    datos: ComandoRequest,
    request: Request,
    db: Session = Depends(get_db),
    usuario: Usuario = Depends(get_current_user),
):
    """
    Enviar un comando al dispositivo vía MQTT.
    El comando se publica en el topic medidor/{device_id}/comando.
    """
    # Validar dispositivo
    dispositivo = db.query(Dispositivo).filter(Dispositivo.device_id == device_id).first()
    if not dispositivo:
        raise HTTPException(status_code=404, detail="Dispositivo no encontrado")

    # Validar comando
    if datos.comando not in COMANDOS_VALIDOS:
        raise HTTPException(
            status_code=400,
            detail=f"Comando inválido: '{datos.comando}'. Comandos válidos: {list(COMANDOS_VALIDOS.keys())}",
        )

    # Verificar cliente MQTT
    mqtt = get_mqtt_client()
    if not mqtt or not mqtt.is_connected:
        raise HTTPException(
            status_code=503,
            detail="Cliente MQTT no disponible. No se puede enviar el comando.",
        )

    # Construir payload
    comando_info = COMANDOS_VALIDOS[datos.comando]
    payload = {**comando_info["payload"]}
    if datos.parametros:
        payload.update(datos.parametros)

    # Publicar en MQTT
    topic = f"medidor/{device_id}/comando"
    try:
        result = mqtt.client.publish(topic, json.dumps(payload), qos=1)
        if result.rc != 0:
            raise Exception(f"MQTT publish failed with rc={result.rc}")
    except Exception as e:
        logger.error(f"❌ Error publicando comando MQTT: {e}")
        raise HTTPException(
            status_code=500,
            detail=f"Error enviando comando: {str(e)}",
        )

    # Registrar en auditoría
    registrar_accion(
        db, accion="comando_enviado", usuario_email=usuario.email,
        recurso="dispositivo", recurso_id=device_id,
        detalles={"comando": datos.comando, "payload": payload},
        ip_address=request.client.host if request.client else None,
    )

    logger.info(f"📤 Comando '{datos.comando}' enviado a {device_id} por {usuario.email}")

    return ComandoResponse(
        exito=True,
        mensaje=f"Comando '{datos.comando}' enviado exitosamente",
        device_id=device_id,
        comando=datos.comando,
        topic=topic,
    )
