import psycopg2

URL = "postgresql://postgres:kaEyZcixoSAlBbKisneqBHjOeqphfXBE@shinkansen.proxy.rlwy.net:16480/railway"

SQL_ALTER = """
ALTER TABLE nodo_salud
    ADD COLUMN IF NOT EXISTS ade_perdidas  INTEGER,
    ADD COLUMN IF NOT EXISTS red_intentos  INTEGER,
    ADD COLUMN IF NOT EXISTS red_exitos    INTEGER,
    ADD COLUMN IF NOT EXISTS mqtt_intentos INTEGER,
    ADD COLUMN IF NOT EXISTS mqtt_exitos   INTEGER;
"""

SQL_CHECK = """
SELECT column_name, data_type
FROM information_schema.columns
WHERE table_name='nodo_salud'
  AND column_name IN ('ade_perdidas','red_intentos','red_exitos','mqtt_intentos','mqtt_exitos')
ORDER BY column_name;
"""

conn = psycopg2.connect(URL)
cur = conn.cursor()
cur.execute(SQL_ALTER)
conn.commit()
cur.execute(SQL_CHECK)
print("Columnas nuevas en nodo_salud:")
for row in cur.fetchall():
    print(" -", row)
cur.close()
conn.close()
print("OK")
