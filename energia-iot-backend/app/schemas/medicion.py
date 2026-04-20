"""SCHEMAS: Medicion - Validación de datos de mediciones de energía"""
from pydantic import BaseModel, Field, field_validator
from typing import Optional, List
from datetime import datetime


class MedicionBase(BaseModel):
    voltaje_rms: float = Field(..., ge=0, le=500)
    corriente_rms: float = Field(..., ge=0, le=200)
    potencia_activa: float = Field(...)
    potencia_reactiva: Optional[float] = Field(default=0.0)
    potencia_aparente: Optional[float] = Field(default=0.0, ge=0)
    factor_potencia: Optional[float] = Field(default=1.0, ge=-1.0, le=1.0)
    frecuencia: Optional[float] = Field(default=60.0, ge=45, le=65)
    thd_voltaje: Optional[float] = Field(default=None, ge=0, le=100)
    thd_corriente: Optional[float] = Field(default=None, ge=0, le=100)
    energia_activa: Optional[float] = Field(default=0.0, ge=0)
    energia_reactiva: Optional[float] = Field(default=0.0)


class MedicionCreate(MedicionBase):
    device_id: str = Field(..., min_length=1, max_length=50)
    timestamp: Optional[datetime] = Field(default=None)
    
    @field_validator('device_id')
    @classmethod
    def normalizar_device_id(cls, v: str) -> str:
        return v.strip().upper()


class MedicionResponse(BaseModel):
    # Respuesta sin validadores ge/le: las mediciones historicas pueden tener
    # freq=0, pf=0, vrms=0 durante arranque/ausencia AC/recovery. Validar
    # rangos aqui produce 500 al serializar filas reales de la BD.
    id: int
    device_id: str
    voltaje_rms: float
    corriente_rms: float
    potencia_activa: float
    potencia_reactiva: Optional[float] = 0.0
    potencia_aparente: Optional[float] = 0.0
    factor_potencia: Optional[float] = 1.0
    frecuencia: Optional[float] = 60.0
    thd_voltaje: Optional[float] = None
    thd_corriente: Optional[float] = None
    energia_activa: Optional[float] = 0.0
    energia_reactiva: Optional[float] = 0.0
    timestamp: datetime
    created_at: datetime

    class Config:
        from_attributes = True


class MedicionResumen(BaseModel):
    device_id: str
    voltaje: str
    corriente: str
    potencia: str
    factor_potencia: str
    energia_kwh: str
    timestamp: datetime


class MedicionHistorico(BaseModel):
    device_id: str
    periodo_inicio: datetime
    periodo_fin: datetime
    total_mediciones: int
    estadisticas: dict
    mediciones: List[MedicionResponse]
