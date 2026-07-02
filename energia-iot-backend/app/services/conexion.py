"""Marca como desconectados los dispositivos que dejaron de publicar.

El flag `conectado` se enciende cuando llega tráfico MQTT y el LWT del
broker lo apaga, pero si el backend estaba caído cuando el broker publicó
el LWT (o el nodo murió sin LWT) el flag queda pegado en True para
siempre. Este refresco por frescura de `ultima_conexion` es la fuente de
verdad: se ejecuta al leer dashboard o listados, de modo que la UI nunca
muestra online un medidor que lleva días mudo.
"""
import logging
from datetime import datetime, timedelta, timezone

from sqlalchemy.orm import Session

from app.config import settings
from app.models.dispositivo import Dispositivo

logger = logging.getLogger(__name__)


def refrescar_estados_conexion(db: Session) -> int:
    """Apaga `conectado` en dispositivos sin tráfico reciente.

    Devuelve cuántos dispositivos cambiaron a offline. La escritura se
    persiste para que el conteo del dashboard y el listado coincidan.
    """
    limite = datetime.now(timezone.utc) - timedelta(minutes=settings.OFFLINE_UMBRAL_MIN)

    afectados = (
        db.query(Dispositivo)
        .filter(
            Dispositivo.conectado == True,  # noqa: E712 — comparación SQL
            (Dispositivo.ultima_conexion == None)  # noqa: E711
            | (Dispositivo.ultima_conexion < limite),
        )
        .update(
            {Dispositivo.conectado: False},
            synchronize_session="fetch",
        )
    )
    if afectados:
        db.commit()
        logger.info(
            "%d dispositivo(s) marcados offline por inactividad (> %d min)",
            afectados, settings.OFFLINE_UMBRAL_MIN,
        )
    return afectados
