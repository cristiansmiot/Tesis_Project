"""
CONFIG - Configuración de la Aplicación
Maneja variables de entorno usando Pydantic Settings.
"""
from pydantic_settings import BaseSettings
from functools import lru_cache
from typing import List


class Settings(BaseSettings):
    """Configuración de la aplicación."""
    
    # General
    ENVIRONMENT: str = "development"
    DEBUG: bool = True
    SECRET_KEY: str = "desarrollo-clave-temporal-cambiar-en-produccion"
    
    # Base de datos
    USE_SQLITE: bool = True  # True = SQLite local, False = PostgreSQL
    DATABASE_URL: str = "postgresql://postgres:password@localhost:5432/energia_iot"
    
    # MQTT
    MQTT_BROKER: str = "shinkansen.proxy.rlwy.net"
    MQTT_PORT: int = 58954
    MQTT_USERNAME: str = "medidor_iot"
    MQTT_PASSWORD: str = ""
    MQTT_TOPIC_BASE: str = "medidor/#"
    MQTT_CLIENT_ID: str = ""
    MQTT_USE_TLS: bool = False  # Railway TCP proxy es passthrough plano, sin TLS
    
    # API
    API_HOST: str = "0.0.0.0"
    API_PORT: int = 8000
    API_PREFIX: str = "/api/v1"
    CORS_ORIGINS: str = "http://localhost:3000,http://localhost:5173"
    
    # Logs
    LOG_LEVEL: str = "INFO"
    
    @property
    def cors_origins_list(self) -> List[str]:
        return [origin.strip() for origin in self.CORS_ORIGINS.split(",")]
    
    class Config:
        env_file = ".env"
        env_file_encoding = "utf-8"
        extra = "ignore"  # Ignorar variables extra en .env


@lru_cache()
def get_settings() -> Settings:
    return Settings()


settings = get_settings()
