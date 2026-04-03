"""MODELO: Usuario - Autenticación y roles del sistema"""
from sqlalchemy import Column, Integer, String, Boolean, DateTime, ForeignKey, UniqueConstraint
from sqlalchemy.orm import relationship
from sqlalchemy.sql import func
from app.database import Base


# Roles válidos del sistema
ROLES_VALIDOS = ["super_admin", "operador", "visualizador"]


class Usuario(Base):
    __tablename__ = "usuarios"

    id = Column(Integer, primary_key=True, index=True)
    email = Column(String(255), unique=True, nullable=False, index=True)
    nombre = Column(String(100), nullable=False)
    apellido = Column(String(100), nullable=False, default="")
    password_hash = Column(String(255), nullable=False)
    rol = Column(String(50), nullable=False, default="visualizador")
    activo = Column(Boolean, default=True)
    created_at = Column(DateTime(timezone=True), server_default=func.now())
    updated_at = Column(DateTime(timezone=True), server_default=func.now(), onupdate=func.now())

    # Dispositivos asignados (solo aplica para visualizador)
    dispositivos_asignados = relationship(
        "UsuarioDispositivo", back_populates="usuario", cascade="all, delete-orphan"
    )

    @property
    def iniciales(self) -> str:
        n = (self.nombre or "")[:1].upper()
        a = (self.apellido or "")[:1].upper()
        return f"{n}{a}" or "U"

    @property
    def es_super_admin(self) -> bool:
        return self.rol == "super_admin"

    @property
    def es_operador(self) -> bool:
        return self.rol == "operador"

    @property
    def es_visualizador(self) -> bool:
        return self.rol == "visualizador"

    @property
    def puede_enviar_comandos(self) -> bool:
        return self.rol in ("super_admin", "operador")

    @property
    def puede_ver_auditoria(self) -> bool:
        return self.rol in ("super_admin", "operador")

    @property
    def puede_gestionar_usuarios(self) -> bool:
        return self.rol == "super_admin"

    def __repr__(self) -> str:
        return f"<Usuario(id={self.id}, email='{self.email}', rol='{self.rol}')>"


class UsuarioDispositivo(Base):
    """Tabla de asignación: qué dispositivos puede ver cada visualizador."""
    __tablename__ = "usuario_dispositivos"

    id = Column(Integer, primary_key=True, index=True)
    usuario_id = Column(Integer, ForeignKey("usuarios.id", ondelete="CASCADE"), nullable=False, index=True)
    device_id = Column(String(50), nullable=False, index=True)
    created_at = Column(DateTime(timezone=True), server_default=func.now())

    usuario = relationship("Usuario", back_populates="dispositivos_asignados")

    __table_args__ = (
        UniqueConstraint("usuario_id", "device_id", name="uq_usuario_dispositivo"),
    )
