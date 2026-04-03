"""MODELO: Usuario - Autenticación y roles del sistema"""
from sqlalchemy import Column, Integer, String, Boolean, DateTime
from sqlalchemy.sql import func
from app.database import Base


class Usuario(Base):
    __tablename__ = "usuarios"

    id = Column(Integer, primary_key=True, index=True)
    email = Column(String(255), unique=True, nullable=False, index=True)
    nombre = Column(String(100), nullable=False)
    apellido = Column(String(100), nullable=False, default="")
    password_hash = Column(String(255), nullable=False)
    rol = Column(String(50), nullable=False, default="Operador")  # Operador, Administrador
    activo = Column(Boolean, default=True)
    created_at = Column(DateTime(timezone=True), server_default=func.now())
    updated_at = Column(DateTime(timezone=True), server_default=func.now(), onupdate=func.now())

    @property
    def iniciales(self) -> str:
        n = (self.nombre or "")[:1].upper()
        a = (self.apellido or "")[:1].upper()
        return f"{n}{a}" or "U"

    def __repr__(self) -> str:
        return f"<Usuario(id={self.id}, email='{self.email}', rol='{self.rol}')>"
