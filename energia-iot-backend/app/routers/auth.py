"""ROUTER: Autenticación - Login, registro, perfil"""
from fastapi import APIRouter, Depends, HTTPException, status, Request
from pydantic import BaseModel, EmailStr
from sqlalchemy.orm import Session
from typing import Optional

from app.database import get_db
from app.models.usuario import Usuario
from app.services.auth import (
    hash_password,
    verify_password,
    create_access_token,
    get_current_user,
)
from app.services.audit import registrar_accion

router = APIRouter(prefix="/auth", tags=["Autenticación"])


# ── Schemas ──────────────────────────────────────────────────────────────────

class LoginRequest(BaseModel):
    email: str
    password: str


class LoginResponse(BaseModel):
    access_token: str
    token_type: str = "bearer"
    usuario: dict


class RegistroRequest(BaseModel):
    email: str
    nombre: str
    apellido: str = ""
    password: str
    rol: str = "Operador"


class CambiarPasswordRequest(BaseModel):
    password_actual: str
    password_nuevo: str


class UsuarioResponse(BaseModel):
    id: int
    email: str
    nombre: str
    apellido: str
    rol: str
    iniciales: str
    activo: bool


# ── Endpoints ────────────────────────────────────────────────────────────────

@router.post("/login", response_model=LoginResponse)
def login(datos: LoginRequest, request: Request, db: Session = Depends(get_db)):
    """Autenticar usuario y obtener JWT."""
    usuario = db.query(Usuario).filter(Usuario.email == datos.email).first()
    if not usuario or not verify_password(datos.password, usuario.password_hash):
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="Credenciales incorrectas",
        )
    if not usuario.activo:
        raise HTTPException(
            status_code=status.HTTP_403_FORBIDDEN,
            detail="Usuario desactivado",
        )

    token = create_access_token(data={"sub": usuario.email})

    registrar_accion(
        db, accion="login", usuario_email=usuario.email,
        ip_address=request.client.host if request.client else None,
    )

    return LoginResponse(
        access_token=token,
        usuario={
            "id": usuario.id,
            "email": usuario.email,
            "nombre": usuario.nombre,
            "apellido": usuario.apellido,
            "rol": usuario.rol,
            "iniciales": usuario.iniciales,
        },
    )


@router.get("/me", response_model=UsuarioResponse)
def obtener_perfil(usuario: Usuario = Depends(get_current_user)):
    """Obtener información del usuario autenticado."""
    return UsuarioResponse(
        id=usuario.id,
        email=usuario.email,
        nombre=usuario.nombre,
        apellido=usuario.apellido,
        rol=usuario.rol,
        iniciales=usuario.iniciales,
        activo=usuario.activo,
    )


@router.post("/registro", response_model=UsuarioResponse, status_code=201)
def registrar_usuario(
    datos: RegistroRequest,
    request: Request,
    db: Session = Depends(get_db),
    usuario_actual: Usuario = Depends(get_current_user),
):
    """Registrar un nuevo usuario (solo administradores)."""
    if usuario_actual.rol != "Administrador":
        raise HTTPException(
            status_code=status.HTTP_403_FORBIDDEN,
            detail="Solo los administradores pueden crear usuarios",
        )

    existente = db.query(Usuario).filter(Usuario.email == datos.email).first()
    if existente:
        raise HTTPException(
            status_code=status.HTTP_400_BAD_REQUEST,
            detail="Ya existe un usuario con ese correo",
        )

    nuevo = Usuario(
        email=datos.email,
        nombre=datos.nombre,
        apellido=datos.apellido,
        password_hash=hash_password(datos.password),
        rol=datos.rol,
    )
    db.add(nuevo)
    db.commit()
    db.refresh(nuevo)

    registrar_accion(
        db, accion="usuario_creado", usuario_email=usuario_actual.email,
        recurso="usuario", recurso_id=str(nuevo.id),
        detalles={"email_nuevo": nuevo.email, "rol": nuevo.rol},
        ip_address=request.client.host if request.client else None,
    )

    return UsuarioResponse(
        id=nuevo.id, email=nuevo.email, nombre=nuevo.nombre,
        apellido=nuevo.apellido, rol=nuevo.rol, iniciales=nuevo.iniciales,
        activo=nuevo.activo,
    )


@router.post("/cambiar-password")
def cambiar_password(
    datos: CambiarPasswordRequest,
    request: Request,
    db: Session = Depends(get_db),
    usuario: Usuario = Depends(get_current_user),
):
    """Cambiar la contraseña del usuario autenticado."""
    if not verify_password(datos.password_actual, usuario.password_hash):
        raise HTTPException(
            status_code=status.HTTP_400_BAD_REQUEST,
            detail="La contraseña actual es incorrecta",
        )

    usuario.password_hash = hash_password(datos.password_nuevo)
    db.commit()

    registrar_accion(
        db, accion="cambio_password", usuario_email=usuario.email,
        ip_address=request.client.host if request.client else None,
    )

    return {"mensaje": "Contraseña actualizada correctamente"}
