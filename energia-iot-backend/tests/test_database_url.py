"""La URL de PostgreSQL debe usar el driver instalado (psycopg2), sin conectar."""
import unittest
from unittest.mock import patch

from sqlalchemy import create_engine

from app.database import get_database_url


class DatabaseUrlTests(unittest.TestCase):
    def url(self, valor):
        with patch("app.database.settings.DATABASE_URL", valor):
            return get_database_url()

    def test_postgres_de_railway_usa_psycopg2(self):
        for valor in ("postgres://u:p@db.internal:5432/railway",
                      "postgresql://u:p@db.internal:5432/railway"):
            url = self.url(valor)
            self.assertTrue(url.startswith("postgresql+psycopg2://"), url)
            engine = create_engine(url)  # no conecta: solo carga el driver
            self.assertEqual(engine.dialect.driver, "psycopg2")
            engine.dispose()

    def test_driver_explicito_no_se_cambia(self):
        url = "postgresql+psycopg2://u:p@db.internal:5432/railway"
        self.assertEqual(self.url(url), url)

    def test_sqlite_local_sin_cambios(self):
        self.assertEqual(self.url("sqlite:///./energia_iot.db"), "sqlite:///./energia_iot.db")


if __name__ == "__main__":
    unittest.main()
