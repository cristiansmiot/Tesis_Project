"""Cierre administrativo de alarmas antiguas. Solo SQLite en memoria."""
import os
os.environ["DEBUG"] = "false"

import json
import unittest
from datetime import datetime, timezone
from types import SimpleNamespace

from sqlalchemy import create_engine
from sqlalchemy.orm import sessionmaker

from app.database import Base
from app.models import Evento
from app.models.audit_log import AuditLog
from app.routers.admin import cerrar_alarmas_antiguas

UTC = timezone.utc
CORTE = datetime(2026, 10, 19, 1, 0, tzinfo=UTC)


class CierreAlarmasTests(unittest.TestCase):
    def setUp(self):
        self.engine = create_engine("sqlite:///:memory:")
        Base.metadata.create_all(self.engine)
        self.db = sessionmaker(bind=self.engine)()
        self.usuario = SimpleNamespace(email="admin@test")
        self.request = SimpleNamespace(client=None)
        for ts, activo, device in (
            (datetime(2026, 10, 18, 19, 0, tzinfo=UTC), True, "SM-1"),
            (datetime(2026, 10, 18, 23, 50, tzinfo=UTC), True, "SM-1"),
            (datetime(2026, 10, 18, 20, 0, tzinfo=UTC), False, "SM-1"),  # ya cerrada
            (datetime(2026, 11, 6, 16, 0, tzinfo=UTC), True, "SM-1"),    # nueva: no se toca
            (datetime(2026, 10, 18, 21, 0, tzinfo=UTC), True, "OTRO"),
        ):
            self.db.add(Evento(device_id=device, tipo="alerta_firmware", mensaje="x",
                               timestamp=ts, activo=activo))
        self.db.commit()

    def tearDown(self):
        self.db.close()
        self.engine.dispose()

    def cerrar(self, **kw):
        kw.setdefault("device_id", None)
        kw.setdefault("confirmar", False)
        return cerrar_alarmas_antiguas(request=self.request, hasta=CORTE, db=self.db,
                                       usuario=self.usuario, **kw)

    def activas(self):
        return self.db.query(Evento).filter(Evento.activo.is_(True)).count()

    def test_simulacion_no_cambia_nada(self):
        r = self.cerrar()
        self.assertTrue(r["simulacion"])
        self.assertEqual(r["total"], 3)
        self.assertEqual(self.activas(), 4)
        self.assertEqual(self.db.query(AuditLog).count(), 0)

    def test_confirmar_cierra_solo_las_anteriores_y_audita(self):
        r = self.cerrar(confirmar=True)
        self.assertEqual(r["cerradas"], 3)
        self.assertEqual(self.activas(), 1)  # la del 06-11 sigue activa
        cerradas = self.db.query(Evento).filter(Evento.reconocido_por == "admin@test").all()
        self.assertEqual(len(cerradas), 3)
        self.assertTrue(all(e.fin_at is not None for e in cerradas))
        log, = self.db.query(AuditLog).all()
        self.assertEqual(log.accion, "alarmas_cerradas_admin")
        self.assertEqual(len(json.loads(log.detalles)["ids"]), 3)
        self.assertEqual(self.db.query(Evento).count(), 5)  # no se borra nada

    def test_filtrar_por_medidor(self):
        r = self.cerrar(device_id="otro", confirmar=True)
        self.assertEqual(r["cerradas"], 1)
        self.assertEqual(self.activas(), 3)


if __name__ == "__main__":
    unittest.main()
