"""Medicion - Representa una lectura de energía del ADE9153A"""
from sqlalchemy import Column, Integer, BigInteger, Boolean, Text, Float, DateTime, ForeignKey, Index, String
from sqlalchemy.orm import relationship
from sqlalchemy.sql import func
from app.database import Base


class Medicion(Base):
    __tablename__ = "mediciones"
    
    id = Column(Integer, primary_key=True, index=True)
    dispositivo_id = Column(Integer, ForeignKey("dispositivos.id", ondelete="CASCADE"), nullable=False, index=True)
    device_id = Column(String(50), nullable=False, index=True)
    voltaje_rms = Column(Float, nullable=False)
    corriente_rms = Column(Float, nullable=False)
    potencia_activa = Column(Float, nullable=False)
    potencia_reactiva = Column(Float, nullable=True, default=0.0)
    potencia_aparente = Column(Float, nullable=True, default=0.0)
    factor_potencia = Column(Float, nullable=True, default=1.0)
    frecuencia = Column(Float, nullable=True, default=60.0)
    thd_voltaje = Column(Float, nullable=True)
    thd_corriente = Column(Float, nullable=True)
    energia_activa = Column(Float, nullable=True, default=0.0)
    energia_reactiva = Column(Float, nullable=True, default=0.0)
    timestamp = Column(DateTime(timezone=True), nullable=False, index=True)
    # Nullable for historical packets lacking trace metadata.
    boot_id = Column(BigInteger, nullable=True)
    seq = Column(BigInteger, nullable=True)
    fw_version = Column(String(20), nullable=True)
    uptime_ms = Column(BigInteger, nullable=True)
    time_valid = Column(Boolean, nullable=True)
    ts_device = Column(DateTime(timezone=True), nullable=True)
    ts_server = Column(DateTime(timezone=True), nullable=True)
    last_received_at = Column(DateTime(timezone=True), nullable=True)
    calibration_status = Column(Integer, nullable=True)
    raw_payload = Column(Text, nullable=True)
    payload_sha256 = Column(String(64), nullable=True)
    duplicate_count = Column(Integer, nullable=False, default=0, server_default="0")
    identity_conflict_count = Column(Integer, nullable=False, default=0, server_default="0")
    created_at = Column(DateTime(timezone=True), server_default=func.now())
    
    dispositivo = relationship("Dispositivo", back_populates="mediciones")
    
    __table_args__ = (
        Index('idx_mediciones_dispositivo_timestamp', 'dispositivo_id', 'timestamp'),
        Index('idx_mediciones_device_timestamp', 'device_id', 'timestamp'),
        Index('uq_mediciones_device_boot_seq', 'device_id', 'boot_id', 'seq', unique=True),
    )
