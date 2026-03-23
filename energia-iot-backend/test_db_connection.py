"""
Script para probar la conexión a PostgreSQL en Railway.

Uso:
    python test_db_connection.py
"""
import os
import sys

# Asegurarse de que el directorio actual esté en el path
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

# Cargar variables de entorno desde .env
from dotenv import load_dotenv
load_dotenv()

print("=" * 60)
print("🔍 PRUEBA DE CONEXIÓN A BASE DE DATOS")
print("=" * 60)

# Mostrar configuración actual
use_sqlite = os.environ.get("USE_SQLITE", "true")
database_url = os.environ.get("DATABASE_URL", "no configurado")

print(f"\n📌 USE_SQLITE: {use_sqlite}")
print(f"📌 DATABASE_URL: {database_url[:50]}..." if len(database_url) > 50 else f"📌 DATABASE_URL: {database_url}")

# Intentar conexión
print("\n🔄 Intentando conectar...")

try:
    from app.database import test_connection, engine
    
    # Mostrar qué tipo de base de datos estamos usando
    print(f"📌 Tipo de BD: {engine.dialect.name}")
    
    # Probar conexión
    if test_connection():
        print("\n" + "=" * 60)
        print("✅ ¡CONEXIÓN EXITOSA!")
        print("=" * 60)
        
        # Si es PostgreSQL, mostrar info adicional
        if engine.dialect.name == "postgresql":
            print("\n🚂 Conectado a Railway PostgreSQL")
            print("   Puedes comenzar a usar la base de datos en la nube.")
    else:
        print("\n❌ No se pudo conectar a la base de datos")
        
except Exception as e:
    print(f"\n❌ Error: {e}")
    print("\n💡 Posibles soluciones:")
    print("   1. Verifica que el archivo .env existe y tiene DATABASE_URL")
    print("   2. Verifica que USE_SQLITE=false en el archivo .env")
    print("   3. Verifica que psycopg2-binary está instalado:")
    print("      pip install psycopg2-binary")
    print("   4. Verifica que la URL de Railway es correcta")
