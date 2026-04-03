"""ROUTERS - Endpoints de la API"""
from app.routers.dispositivos import router as dispositivos_router
from app.routers.mediciones import router as mediciones_router
from app.routers.salud import router as salud_router
from app.routers.auth import router as auth_router
from app.routers.eventos import router as eventos_router
from app.routers.comandos import router as comandos_router
from app.routers.auditoria import router as auditoria_router
from app.routers.dashboard import router as dashboard_router
__all__ = [
    "dispositivos_router", "mediciones_router", "salud_router",
    "auth_router", "eventos_router", "comandos_router",
    "auditoria_router", "dashboard_router",
]
