"""ROUTERS - Endpoints de la API"""
from app.routers.dispositivos import router as dispositivos_router
from app.routers.mediciones import router as mediciones_router
from app.routers.salud import router as salud_router
__all__ = ["dispositivos_router", "mediciones_router", "salud_router"]
