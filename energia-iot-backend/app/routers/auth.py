"""ROUTER: Autenticación - Login, registro, perfil, gestión de usuarios"""
from fastapi import APIRouter, Depends, HTTPException, status, Request
from pydantic import BaseModel
from sqlalchemy.orm import Session
from typing import Optional, List

from app.database import get_db
from app.models.usuario import Usuario, UsuarioDispositivo, ROLES_VALIDOS
from app.models.dispositivo import Dispositivo
from app.services.auth import (
    hash_password,
    verify_password,
    create_access_token,
    get_current_user,
    require_super_admin,
    obtener_dispositivos_asignados,
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
    rol: str = "visualizador"


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
    dispositivos_asignados: Optional[List[str]] = None


class CambiarRolRequest(BaseModel):
    rol: str


class AsignarDispositivosRequest(BaseModel):
    device_ids: List[str]


# ── Endpoints de Autenticación ──────────────────────────────────────────────

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

    # Obtener dispositivos asignados si es visualizador
    dispositivos = obtener_dispositivos_asignados(usuario, db)

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
            "dispositivos_asignados": dispositivos,
        },
    )


@router.get("/me", response_model=UsuarioResponse)
def obtener_perfil(usuario: Usuario = Depends(get_current_user), db: Session = Depends(get_db)):
    """Obtener información del usuario autenticado."""
    dispositivos = obtener_dispositivos_asignados(usuario, db)
    return UsuarioResponse(
        id=usuario.id,
        email=usuario.email,
        nombre=usuario.nombre,
        apellido=usuario.apellido,
        rol=usuario.rol,
        iniciales=usuario.iniciales,
        activo=usuario.activo,
        dispositivos_asignados=dispositivos,
    )


@router.post("/registro", response_model=UsuarioResponse, status_code=201)
def registrar_usuario(
    datos: RegistroRequest,
    request: Request,
    db: Session = Depends(get_db),
    usuario_actual: Usuario = Depends(require_super_admin),
):
    """Registrar un nuevo usuario (solo super_admin)."""
    if datos.rol not in ROLES_VALIDOS:
        raise HTTPException(
            status_code=status.HTTP_400_BAD_REQUEST,
            detail=f"Rol inválido. Roles válidos: {', '.join(ROLES_VALIDOS)}",
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


# ── Gestión de Usuarios (solo super_admin) ──────────────────────────────────

@router.get("/usuarios", response_model=List[UsuarioResponse])
def listar_usuarios(
    db: Session = Depends(get_db),
    _admin: Usuario = Depends(require_super_admin),
):
    """Listar todos los usuarios del sistema (solo super_admin)."""
    usuarios = db.query(Usuario).order_by(Usuario.created_at).all()
    resultado = []
    for u in usuarios:
        dispositivos = obtener_dispositivos_asignados(u, db)
        resultado.append(UsuarioResponse(
            id=u.id, email=u.email, nombre=u.nombre, apellido=u.apellido,
            rol=u.rol, iniciales=u.iniciales, activo=u.activo,
            dispositivos_asignados=dispositivos,
        ))
    return resultado


@router.patch("/usuarios/{usuario_id}/rol")
def cambiar_rol(
    usuario_id: int,
    datos: CambiarRolRequest,
    request: Request,
    db: Session = Depends(get_db),
    admin: Usuario = Depends(require_super_admin),
):
    """Cambiar el rol de un usuario (solo super_admin)."""
    if datos.rol not in ROLES_VALIDOS:
        raise HTTPException(
            status_code=status.HTTP_400_BAD_REQUEST,
            detail=f"Rol inválido. Roles válidos: {', '.join(ROLES_VALIDOS)}",
        )

    usuario = db.query(Usuario).filter(Usuario.id == usuario_id).first()
    if not usuario:
        raise HTTPException(status_code=404, detail="Usuario no encontrado")

    if usuario.id == admin.id:
        raise HTTPException(
            status_code=status.HTTP_400_BAD_REQUEST,
            detail="No puedes cambiar tu propio rol",
        )

    rol_anterior = usuario.rol
    usuario.rol = datos.rol
    db.commit()

    registrar_accion(
        db, accion="cambio_rol", usuario_email=admin.email,
        recurso="usuario", recurso_id=str(usuario.id),
        detalles={"email": usuario.email, "rol_anterior": rol_anterior, "rol_nuevo": datos.rol},
        ip_address=request.client.host if request.client else None,
    )

    return {"mensaje": f"Rol de {usuario.email} cambiado de {rol_anterior} a {datos.rol}"}


@router.patch("/usuarios/{usuario_id}/activo")
def toggle_activo(
    usuario_id: int,
    request: Request,
    db: Session = Depends(get_db),
    admin: Usuario = Depends(require_super_admin),
):
    """Activar/desactivar un usuario (solo super_admin)."""
    usuario = db.query(Usuario).filter(Usuario.id == usuario_id).first()
    if not usuario:
        raise HTTPException(status_code=404, detail="Usuario no encontrado")

    if usuario.id == admin.id:
        raise HTTPException(status_code=400, detail="No puedes desactivarte a ti mismo")

    usuario.activo = not usuario.activo
    db.commit()

    registrar_accion(
        db, accion="usuario_activado" if usuario.activo else "usuario_desactivado",
        usuario_email=admin.email, recurso="usuario", recurso_id=str(usuario.id),
        detalles={"email": usuario.email},
        ip_address=request.client.host if request.client else None,
    )

    return {"mensaje": f"Usuario {usuario.email} {'activado' if usuario.activo else 'desactivado'}"}


@router.put("/usuarios/{usuario_id}/dispositivos")
def asignar_dispositivos(
    usuario_id: int,
    datos: AsignarDispositivosRequest,
    request: Request,
    db: Session = Depends(get_db),
    admin: Usuario = Depends(require_super_admin),
):
    """
    Asignar dispositivos a un usuario visualizador (solo super_admin).
    Reemplaza todas las asignaciones existentes.
    """
    usuario = db.query(Usuario).filter(Usuario.id == usuario_id).first()
    if not usuario:
        raise HTTPException(status_code=404, detail="Usuario no encontrado")

    # Verificar que los device_ids existan
    for device_id in datos.device_ids:
        disp = db.query(Dispositivo).filter(Dispositivo.device_id == device_id).first()
        if not disp:
            raise HTTPException(status_code=404, detail=f"Dispositivo '{device_id}' no encontrado")

    # Eliminar asignaciones existentes
    db.query(UsuarioDispositivo).filter(
        UsuarioDispositivo.usuario_id == usuario_id
    ).delete()

    # Crear nuevas asignaciones
    for device_id in datos.device_ids:
        asignacion = UsuarioDispositivo(usuario_id=usuario_id, device_id=device_id)
        db.add(asignacion)

    db.commit()

    registrar_accion(
        db, accion="dispositivos_asignados", usuario_email=admin.email,
        recurso="usuario", recurso_id=str(usuario.id),
        detalles={"email": usuario.email, "device_ids": datos.device_ids},
        ip_address=request.client.host if request.client else None,
    )

    return {
        "mensaje": f"{len(datos.device_ids)} dispositivo(s) asignados a {usuario.email}",
        "device_ids": datos.device_ids,
    }
