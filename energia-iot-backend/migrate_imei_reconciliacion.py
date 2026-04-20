"""Migración: añadir columna imei a nodo_salud"""
import psycopg2

URL = "postgresql://postgres:kaEyZcixoSAlBbKisneqBHjOeqphfXBE@66.33.22.231:16480/railway"

SQL = """
ALTER TABLE nodo_salud
    ADD COLUMN IF NOT EXISTS imei VARCHAR(20);
"""

CHECK = """
SELECT column_name, data_type
FROM information_schema.columns
WHERE table_name='nodo_salud' AND column_name='imei';
"""

conn = psycopg2.connect(URL)
cur = conn.cursor()
cur.execute(SQL)
conn.commit()
cur.execute(CHECK)
rows = cur.fetchall()
print("Columnas nuevas:", rows)
cur.close()
conn.close()
print("OK")
