"""MODELO: Dispositivo - Representa un medidor de energía ESP32"""
from sqlalchemy import Column, Integer, String, Boolean, DateTime, Float, Text
from sqlalchemy.orm import relationship
from sqlalchemy.sql import func
from app.database import Base


class Dispositivo(Base):
    __tablename__ = "dispositivos"
    
    id = Column(Integer, primary_key=True, index=True)
    device_id = Column(String(50), unique=True, nullable=False, index=True)
    nombre = Column(String(100), nullable=True, default="Medidor Sin Nombre")
    ubicacion = Column(String(200), nullable=True)
    descripcion = Column(Text, nullable=True)
    activo = Column(Boolean, default=True)
    conectado = Column(Boolean, default=False)
    ultima_conexion = Column(DateTime(timezone=True), nullable=True)
    firmware_version = Column(String(20), nullable=True, default="1.0.0")
    intervalo_medicion = Column(Integer, default=60)
    factor_voltaje = Column(Float, default=1.0)
    factor_corriente = Column(Float, default=1.0)
    created_at = Column(DateTime(timezone=True), server_default=func.now())
    updated_at = Column(DateTime(timezone=True), server_default=func.now(), onupdate=func.now())
    
    mediciones = relationship("Medicion", back_populates="dispositivo", cascade="all, delete-orphan", lazy="dynamic")
    
    def __repr__(self) -> str:
        return f"<Dispositivo(id={self.id}, device_id='{self.device_id}')>"
