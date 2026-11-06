"""Consumo del dashboard a partir del contador. Solo SQLite en memoria."""
import os
os.environ["DEBUG"] = "false"

import unittest
from datetime import datetime, timedelta, timezone

from sqlalchemy import create_engine
from sqlalchemy.orm import sessionmaker

from app.database import Base
from app.models import Dispositivo, Medicion
from app.routers.dashboard import obtener_consumo_horario, obtener_resumen
from app.services.consumo import (
    BOGOTA, consumo_por_hora, consumo_wh, inicio_dia_local, inicio_hora_local,
)

UTC = timezone.utc


def local(*args):
    """Fecha y hora de Bogotá expresada en UTC."""
    return datetime(*args, tzinfo=BOGOTA).astimezone(UTC)


class DashboardConsumoTests(unittest.TestCase):
    def setUp(self):
        self.engine = create_engine("sqlite:///:memory:")
        Base.metadata.create_all(self.engine)
        self.db = sessionmaker(bind=self.engine)()
        self.dispositivos = {}

    def tearDown(self):
        self.db.close()
        self.engine.dispose()

    def lectura(self, device_id, ts, energia_wh, voltaje=120.0):
        disp = self.dispositivos.get(device_id)
        if disp is None:
            disp = Dispositivo(device_id=device_id, activo=True, conectado=False)
            self.db.add(disp)
            self.db.flush()
            self.dispositivos[device_id] = disp
        self.db.add(Medicion(
            dispositivo_id=disp.id, device_id=device_id, timestamp=ts,
            voltaje_rms=voltaje, corriente_rms=0.2, potencia_activa=27.0,
            energia_activa=energia_wh,
        ))
        self.db.commit()

    def test_dia_local_de_bogota(self):
        # 00:30 de Bogota son las 05:30 UTC: el dia local empieza a las 05:00 UTC.
        self.assertEqual(inicio_dia_local(datetime(2026, 11, 6, 3, 0, tzinfo=UTC)),
                         datetime(2026, 11, 5, 5, 0, tzinfo=UTC))
        self.assertEqual(inicio_hora_local(datetime(2026, 11, 6, 5, 30, tzinfo=UTC)),
                         datetime(2026, 11, 6, 5, 0, tzinfo=UTC))

    def test_consumo_del_dia_es_diferencia_del_contador_no_suma(self):
        ahora = local(2026, 11, 6, 12, 0)
        self.lectura("A", local(2026, 11, 5, 23, 55), 100.0)  # base: < 1 h antes
        self.lectura("A", local(2026, 11, 6, 0, 5), 102.0)
        self.lectura("A", local(2026, 11, 6, 6, 0), 110.0)
        self.lectura("B", local(2026, 11, 6, 1, 0), 5000.0)
        self.lectura("B", local(2026, 11, 6, 2, 0), 5003.0)
        self.assertAlmostEqual(consumo_wh(self.db, inicio_dia_local(ahora), ahora), 13.0)

    def test_base_antigua_no_se_atribuye_a_la_ventana(self):
        ahora = local(2026, 11, 6, 12, 0)
        self.lectura("A", local(2026, 11, 5, 21, 0), 100.0)  # 3 h antes de medianoche
        self.lectura("A", local(2026, 11, 6, 3, 0), 140.0)
        self.lectura("A", local(2026, 11, 6, 3, 5), 141.0)
        self.assertAlmostEqual(consumo_wh(self.db, inicio_dia_local(ahora), ahora), 1.0)

    def test_reinicio_del_contador_no_resta(self):
        ahora = local(2026, 11, 6, 12, 0)
        for minuto, energia in ((0, 100.0), (5, 105.0), (10, 2.0), (15, 4.0)):
            self.lectura("A", local(2026, 11, 6, 8, minuto), energia)
        self.assertAlmostEqual(consumo_wh(self.db, inicio_dia_local(ahora), ahora), 7.0)

    def test_horas_en_hora_local_y_con_hueco(self):
        ahora = local(2026, 11, 6, 10, 20)
        self.lectura("A", local(2026, 11, 6, 0, 10), 50.0)
        self.lectura("A", local(2026, 11, 6, 0, 40), 52.5)
        self.lectura("A", local(2026, 11, 6, 3, 15), 60.0)  # tras 2.5 h sin datos
        horas = consumo_por_hora(self.db, 24, ahora)
        self.assertEqual(len(horas), 24)
        self.assertEqual(horas[-1]["inicio"], local(2026, 11, 6, 10, 0))
        por_inicio = {h["inicio"]: h for h in horas}
        cero = por_inicio[local(2026, 11, 6, 0, 0)]
        self.assertAlmostEqual(cero["wh"], 2.5)
        self.assertEqual(cero["muestras"], 2)
        tres = por_inicio[local(2026, 11, 6, 3, 0)]
        self.assertAlmostEqual(tres["wh"], 7.5)
        self.assertTrue(tres["con_hueco"])
        self.assertEqual(por_inicio[local(2026, 11, 6, 1, 0)]["muestras"], 0)

    def test_endpoints_con_hora_actual(self):
        ahora = datetime.now(UTC)
        self.lectura("A", ahora - timedelta(minutes=10), 1000.0, voltaje=121.0)
        self.lectura("A", ahora - timedelta(minutes=5), 1003.0, voltaje=122.0)
        resumen = obtener_resumen(db=self.db)
        self.assertAlmostEqual(resumen.consumo_dia_kwh, 0.003)
        self.assertEqual(resumen.consumo_total_kwh, resumen.consumo_dia_kwh)
        self.assertAlmostEqual(resumen.energia_acumulada_kwh, 1.003)
        self.assertEqual(resumen.voltaje_promedio, 122.0)
        horario = obtener_consumo_horario(horas=24, db=self.db)
        self.assertEqual(len(horario.datos), 24)
        self.assertAlmostEqual(sum(d.wh for d in horario.datos), 3.0)
        self.assertEqual(horario.datos[-1].hora, ahora.astimezone(BOGOTA).strftime("%H:00"))

    def test_ultima_lectura_por_tiempo_y_no_por_id(self):
        ahora = datetime.now(UTC)
        self.lectura("A", ahora - timedelta(minutes=1), 2000.0, voltaje=119.0)
        # Backlog de la SD: llega despues (id mayor) con una lectura antigua.
        self.lectura("A", ahora - timedelta(hours=30), 1500.0, voltaje=90.0)
        resumen = obtener_resumen(db=self.db)
        self.assertAlmostEqual(resumen.energia_acumulada_kwh, 2.0)
        self.assertEqual(resumen.voltaje_promedio, 119.0)
        self.assertEqual(resumen.dispositivos_fuera_rango, 0)


if __name__ == "__main__":
    unittest.main()
