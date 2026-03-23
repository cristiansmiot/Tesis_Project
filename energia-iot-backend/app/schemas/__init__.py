"""SCHEMAS - Esquemas de Validación (Pydantic)"""
from app.schemas.dispositivo import DispositivoBase, DispositivoCreate, DispositivoUpdate, DispositivoResponse, DispositivoEstado
from app.schemas.medicion import MedicionBase, MedicionCreate, MedicionResponse, MedicionResumen, MedicionHistorico
__all__ = ["DispositivoBase", "DispositivoCreate", "DispositivoUpdate", "DispositivoResponse", "DispositivoEstado",
           "MedicionBase", "MedicionCreate", "MedicionResponse", "MedicionResumen", "MedicionHistorico"]
