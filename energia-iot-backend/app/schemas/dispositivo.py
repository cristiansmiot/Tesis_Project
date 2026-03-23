"""SCHEMAS: Dispositivo - Validación de datos con Pydantic"""
from pydantic import BaseModel, Field, field_validator
from typing import Optional
from datetime import datetime


class DispositivoBase(BaseModel):
    device_id: str = Field(..., min_length=1, max_length=50)
    nombre: Optional[str] = Field(default="Medidor Sin Nombre", max_length=100)
    ubicacion: Optional[str] = Field(default=None, max_length=200)
    descripcion: Optional[str] = Field(default=None)
    
    @field_validator('device_id')
    @classmethod
    def validar_device_id(cls, v: str) -> str:
        return v.strip().upper()


class DispositivoCreate(DispositivoBase):
    intervalo_medicion: Optional[int] = Field(default=60, ge=10, le=3600)


class DispositivoUpdate(BaseModel):
    nombre: Optional[str] = Field(default=None, max_length=100)
    ubicacion: Optional[str] = Field(default=None, max_length=200)
    descripcion: Optional[str] = Field(default=None)
    activo: Optional[bool] = Field(default=None)
    intervalo_medicion: Optional[int] = Field(default=None, ge=10, le=3600)


class DispositivoResponse(DispositivoBase):
    id: int
    activo: bool
    conectado: bool
    ultima_conexion: Optional[datetime] = None
    firmware_version: Optional[str] = None
    intervalo_medicion: int
    factor_voltaje: float
    factor_corriente: float
    created_at: datetime
    updated_at: datetime
    
    class Config:
        from_attributes = True


class DispositivoEstado(BaseModel):
    device_id: str
    nombre: str
    conectado: bool
    ultima_conexion: Optional[datetime]
    tiempo_sin_conexion: Optional[str] = None
    
    class Config:
        from_attributes = True
