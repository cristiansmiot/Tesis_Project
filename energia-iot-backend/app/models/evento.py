"""MODELO: Evento - Alertas y eventos del sistema"""
from sqlalchemy import Column, Integer, String, Float, Boolean, DateTime, Text
from sqlalchemy.sql import func
from app.database import Base


class Evento(Base):
    __tablename__ = "eventos"

    id = Column(Integer, primary_key=True, index=True)
    device_id = Column(String(50), nullable=False, index=True)
    tipo = Column(String(50), nullable=False, index=True)  # sobrevoltaje, subtension, desconexion, comunicacion_debil, alerta_firmware
    severidad = Column(String(20), nullable=False, default="warning")  # info, warning, critical
    valor = Column(Float, nullable=True)  # Valor que disparó el evento (ej: 135.5 V)
    umbral = Column(Float, nullable=True)  # Umbral configurado (ej: 132.0 V)
    mensaje = Column(Text, nullable=False)
    activo = Column(Boolean, default=True)  # True = no reconocida, False = reconocida
    reconocido_por = Column(String(255), nullable=True)  # email del usuario que reconoció
    reconocido_at = Column(DateTime(timezone=True), nullable=True)
    timestamp = Column(DateTime(timezone=True), server_default=func.now(), index=True)
    created_at = Column(DateTime(timezone=True), server_default=func.now())

    def __repr__(self) -> str:
        return f"<Evento(id={self.id}, device_id='{self.device_id}', tipo='{self.tipo}')>"
