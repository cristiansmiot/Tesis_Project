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
    # USE_SQLITE se auto-detecta: si DATABASE_URL contiene 'postgresql', usa PostgreSQL
    USE_SQLITE: bool = True  # Se sobreescribe en auto-detección abajo
    DATABASE_URL: str = "sqlite:///./energia_iot.db"
    # Railway inyecta DATABASE_URL automáticamente si se configura como variable referenciada
    
    # MQTT
    MQTT_BROKER: str = "shinkansen.proxy.rlwy.net"
    MQTT_PORT: int = 58954
    MQTT_USERNAME: str = "medidor_iot"
    MQTT_PASSWORD: str = ""
    MQTT_TOPIC_BASE: str = "medidor/#"
    MQTT_CLIENT_ID: str = ""
    MQTT_USE_TLS: bool = True
    # Ruta al ca.crt de la CA propia del proyecto (mosquitto-config/certs).
    # Vacío = validar contra las CAs del sistema (no sirve para el broker
    # detrás del proxy Railway, cuyo certificado emite nuestra CA).
    MQTT_TLS_CA_FILE: str = ""
    
    # API
    API_HOST: str = "0.0.0.0"
    API_PORT: int = 8000
    API_PREFIX: str = "/api/v1"
    CORS_ORIGINS: str = "http://localhost:3000,http://localhost:5173"
    
    # Detección de desconexión: un medidor se considera offline si no ha
    # publicado nada (datos, estado o conexión) en este tiempo. El firmware
    # publica /datos cada 60 s, así que 5 minutos tolera varios reintentos
    # de red antes de declarar la caída.
    OFFLINE_UMBRAL_MIN: int = 5

    # Calidad de tensión residencial de nivel 1:
    # - nominal 120 V fase-tierra (RETIE 2024),
    # - banda estacionaria 90 %..110 % por más de un minuto
    #   (CREG 070/1998, num. 6.2.1.1, mod. CREG 153/2019),
    # - confirmada como 108 V..132 V por el Concepto CREG 6068/2025.
    # Configurable por entorno para instalaciones con otro nominal declarado.
    VOLTAJE_NOMINAL: float = 120.0
    VOLTAJE_TOLERANCIA_PCT: float = 10.0
    # La condición debe mantenerse al menos un minuto antes de levantar alarma.
    VOLTAJE_PERSISTENCIA_S: int = 60

    # Logs
    LOG_LEVEL: str = "INFO"

    @property
    def voltaje_min(self) -> float:
        # round() evita residuos binarios de punto flotante.
        return round(self.VOLTAJE_NOMINAL * (1 - self.VOLTAJE_TOLERANCIA_PCT / 100), 2)

    @property
    def voltaje_max(self) -> float:
        return round(self.VOLTAJE_NOMINAL * (1 + self.VOLTAJE_TOLERANCIA_PCT / 100), 2)
    
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
