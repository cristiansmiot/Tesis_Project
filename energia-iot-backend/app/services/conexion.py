"""Marca como desconectados los dispositivos que dejaron de publicar.

La frescura de `ultima_conexion` (last_seen) es la fuente de verdad. El
refresco se ejecuta al leer dashboard o listados, de modo que la UI no depende
de LWT ni muestra online un medidor que lleva días mudo.
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
