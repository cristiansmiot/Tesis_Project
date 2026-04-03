"""
SERVICIO: Autenticación JWT + Control de Acceso por Roles
Maneja hash de contraseñas, generación y validación de tokens JWT,
y dependencias de verificación de rol (RBAC).

Roles del sistema:
  - super_admin: Todo + gestión de usuarios y roles
  - operador: Todo operativo, sin gestión de usuarios, auditoria propia
  - visualizador: Solo lectura, medidores asignados, sin comandos ni auditoría
"""
import logging
from datetime import datetime, timedelta, timezone
from typing import Optional, List
from functools import wraps

from fastapi import Depends, HTTPException, status
from fastapi.security import HTTPBearer, HTTPAuthorizationCredentials
from jose import JWTError, jwt
from passlib.context import CryptContext
from sqlalchemy.orm import Session

from app.config import settings
from app.database import get_db
from app.models.usuario import Usuario, UsuarioDispositivo

logger = logging.getLogger(__name__)

# Configuración JWT
SECRET_KEY = settings.SECRET_KEY
ALGORITHM = "HS256"
ACCESS_TOKEN_EXPIRE_MINUTES = 60 * 24  # 24 horas

# Hashing de contraseñas
pwd_context = CryptContext(schemes=["bcrypt"], deprecated="auto")

# Esquema de seguridad
security = HTTPBearer()


def hash_password(password: str) -> str:
    """Genera hash bcrypt de una contraseña."""
    return pwd_context.hash(password)


def verify_password(plain_password: str, hashed_password: str) -> bool:
    """Verifica una contraseña contra su hash."""
    return pwd_context.verify(plain_password, hashed_password)


def create_access_token(data: dict, expires_delta: Optional[timedelta] = None) -> str:
    """Genera un JWT access token."""
    to_encode = data.copy()
    expire = datetime.now(timezone.utc) + (expires_delta or timedelta(minutes=ACCESS_TOKEN_EXPIRE_MINUTES))
    to_encode.update({"exp": expire})
    return jwt.encode(to_encode, SECRET_KEY, algorithm=ALGORITHM)


def decode_token(token: str) -> dict:
    """Decodifica y valida un JWT token."""
    try:
        payload = jwt.decode(token, SECRET_KEY, algorithms=[ALGORITHM])
        return payload
    except JWTError:
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="Token inválido o expirado",
            headers={"WWW-Authenticate": "Bearer"},
        )


def get_current_user(
    credentials: HTTPAuthorizationCredentials = Depends(security),
    db: Session = Depends(get_db),
) -> Usuario:
    """Dependencia FastAPI: obtiene el usuario actual del JWT."""
    payload = decode_token(credentials.credentials)
    email: str = payload.get("sub")
    if email is None:
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="Token inválido: falta campo 'sub'",
        )

    usuario = db.query(Usuario).filter(Usuario.email == email).first()
    if usuario is None:
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="Usuario no encontrado",
        )
    if not usuario.activo:
        raise HTTPException(
            status_code=status.HTTP_403_FORBIDDEN,
            detail="Usuario desactivado",
        )
    return usuario


def get_optional_user(
    credentials: Optional[HTTPAuthorizationCredentials] = Depends(HTTPBearer(auto_error=False)),
    db: Session = Depends(get_db),
) -> Optional[Usuario]:
    """Dependencia opcional: retorna el usuario si hay token, None si no."""
    if credentials is None:
        return None
    try:
        return get_current_user(credentials, db)
    except HTTPException:
        return None


# ── Dependencias de Verificación de Rol ─────────────────────────────────────

def require_role(*roles_permitidos: str):
    """
    Fábrica de dependencias: verifica que el usuario tenga uno de los roles permitidos.

    Uso:
        @router.get("/admin", dependencies=[Depends(require_role("super_admin"))])
        def solo_admin(): ...

        @router.post("/cmd", dependencies=[Depends(require_role("super_admin", "operador"))])
        def operadores_y_admin(): ...
    """
    def dependency(usuario: Usuario = Depends(get_current_user)):
        if usuario.rol not in roles_permitidos:
            raise HTTPException(
                status_code=status.HTTP_403_FORBIDDEN,
                detail=f"Acceso denegado. Se requiere rol: {', '.join(roles_permitidos)}. Tu rol: {usuario.rol}",
            )
        return usuario
    return dependency


def require_super_admin(usuario: Usuario = Depends(get_current_user)) -> Usuario:
    """Dependencia: solo super_admin puede acceder."""
    if usuario.rol != "super_admin":
        raise HTTPException(
            status_code=status.HTTP_403_FORBIDDEN,
            detail="Solo el Super Administrador puede realizar esta acción",
        )
    return usuario


def require_operador_or_admin(usuario: Usuario = Depends(get_current_user)) -> Usuario:
    """Dependencia: super_admin u operador pueden acceder."""
    if usuario.rol not in ("super_admin", "operador"):
        raise HTTPException(
            status_code=status.HTTP_403_FORBIDDEN,
            detail="Solo Operadores y Super Administradores pueden realizar esta acción",
        )
    return usuario


# ── Funciones de Asignación de Dispositivos ─────────────────────────────────

def obtener_dispositivos_asignados(usuario: Usuario, db: Session) -> Optional[List[str]]:
    """
    Retorna la lista de device_ids asignados al usuario.
    - super_admin / operador → None (acceso a todos)
    - visualizador → lista de device_ids asignados
    """
    if usuario.rol in ("super_admin", "operador"):
        return None  # Sin restricción

    asignaciones = db.query(UsuarioDispositivo.device_id).filter(
        UsuarioDispositivo.usuario_id == usuario.id
    ).all()
    return [a.device_id for a in asignaciones]


# ── Seed de Admin por Defecto ───────────────────────────────────────────────

def create_default_admin(db: Session):
    """Crea un usuario super_admin por defecto si no existe ninguno."""
    existing = db.query(Usuario).first()
    if existing:
        # Migración: si existe usuario con rol viejo, actualizar
        admins = db.query(Usuario).filter(Usuario.rol == "Administrador").all()
        for admin in admins:
            admin.rol = "super_admin"
            logger.info(f"🔄 Rol migrado: {admin.email} Administrador → super_admin")
        operadores = db.query(Usuario).filter(Usuario.rol == "Operador").all()
        for op in operadores:
            op.rol = "operador"
            logger.info(f"🔄 Rol migrado: {op.email} Operador → operador")
        if admins or operadores:
            db.commit()
        return

    admin = Usuario(
        email="admin@medidoriot.com",
        nombre="Admin",
        apellido="Sistema",
        password_hash=hash_password("admin123"),
        rol="super_admin",
        activo=True,
    )
    db.add(admin)
    db.commit()
    logger.info("👤 Usuario super_admin creado: admin@medidoriot.com / admin123")
