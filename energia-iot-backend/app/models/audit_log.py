"""MODELO: AuditLog - Registro de auditoría del sistema"""
from sqlalchemy import Column, Integer, String, DateTime, Text
from sqlalchemy.sql import func
from app.database import Base


class AuditLog(Base):
    __tablename__ = "audit_logs"

    id = Column(Integer, primary_key=True, index=True)
    usuario_email = Column(String(255), nullable=True, index=True)  # Null para acciones del sistema
    accion = Column(String(100), nullable=False, index=True)  # login, logout, comando_enviado, alerta_reconocida, dispositivo_editado
    recurso = Column(String(100), nullable=True)  # dispositivo, evento, usuario
    recurso_id = Column(String(100), nullable=True)  # ID del recurso afectado
    detalles = Column(Text, nullable=True)  # JSON con detalles adicionales
    ip_address = Column(String(45), nullable=True)
    timestamp = Column(DateTime(timezone=True), server_default=func.now(), index=True)

    def __repr__(self) -> str:
        return f"<AuditLog(id={self.id}, accion='{self.accion}', usuario='{self.usuario_email}')>"
