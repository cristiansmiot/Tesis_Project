"""No broker, deployment or existing DB is touched: SQLite in memory only."""
import os
os.environ["DEBUG"] = "false"

import unittest
import json
import tempfile
from pathlib import Path
from datetime import datetime, timezone, timedelta
from types import SimpleNamespace
from sqlalchemy import create_engine, inspect, text
from sqlalchemy.orm import sessionmaker

from app.database import Base, _aplicar_migraciones_incrementales
from app.models import Medicion, NodoSalud, Dispositivo, Evento
from app.services.mqtt_client import MQTTClient
from app.services.telemetry_contract import trace_metadata, sequence_metrics
from tools.reconcile_obj4 import reconcile, flatten, load_records


class TelemetryContractTests(unittest.TestCase):
    def setUp(self):
        self.engine = create_engine("sqlite:///:memory:")
        Base.metadata.create_all(self.engine)
        self.sessions = sessionmaker(bind=self.engine)
        self.client = MQTTClient("unused.invalid", 1883, "test", "test", use_tls=False)
        self.client.set_db_session_factory(self.sessions)
        self.received = datetime(2026, 10, 20, 15, 0, 3, tzinfo=timezone.utc)
        self.data = {"boot_id": 4294967295, "seq": 1, "fw": "1.1.0-obj4",
            "uptime_ms": 60001, "ts_device": self.received.timestamp() - 3,
            "time_valid": True, "vrms": 120.0, "irms": 1.0,
            "p_act": 120.0, "pf": 1.0, "freq": 60.0, "e_act": 2.0, "cal": 14}

    def tearDown(self):
        self.engine.dispose()

    def measurements(self):
        with self.sessions() as db:
            return db.query(Medicion).all()

    def test_identity_time_and_firmware_persist(self):
        self.client._process_measurement("TEST-1", self.data, self.received)
        row, = self.measurements()
        self.assertEqual(row.boot_id, 4294967295)
        self.assertEqual(row.seq, 1)
        self.assertEqual(row.fw_version, "1.1.0-obj4")
        self.assertEqual((row.ts_server - row.ts_device).total_seconds(), 3)
        self.assertEqual(row.calibration_status, 14)

    def test_replay_keeps_original_arrival_and_counts_duplicates(self):
        self.client._process_measurement("TEST-1", self.data, self.received)
        self.client._process_measurement("TEST-1", self.data, self.received + timedelta(hours=1))
        row, = self.measurements()
        self.assertEqual(row.duplicate_count, 1)
        self.assertEqual(row.identity_conflict_count, 0)
        self.assertEqual((row.ts_server - row.ts_device).total_seconds(), 3)
        self.assertEqual((row.last_received_at - row.ts_server).total_seconds(), 3600)

    def test_same_sequence_new_boot_is_a_new_measurement(self):
        self.client._process_measurement("TEST-1", self.data, self.received)
        self.client._process_measurement("TEST-1", dict(self.data, boot_id=7), self.received)
        self.assertEqual(len(self.measurements()), 2)

    def test_conflicting_replay_is_flagged_and_original_preserved(self):
        self.client._process_measurement("TEST-1", self.data, self.received)
        self.client._process_measurement("TEST-1", dict(self.data, vrms=999), self.received)
        row, = self.measurements()
        self.assertEqual(row.voltaje_rms, 120)
        self.assertEqual(row.identity_conflict_count, 1)

    def test_invalid_clock_never_becomes_a_latency_sample(self):
        trace = trace_metadata(dict(self.data, time_valid=False), self.received)
        self.assertIsNone(trace["ts_device"])
        self.assertFalse(trace["time_valid"])

    def test_legacy_is_saved_without_inventing_identity(self):
        self.client._process_measurement("TEST-1", {"vrms": 120}, self.received)
        self.client._process_measurement("TEST-1", {"vrms": 120}, self.received)
        self.assertEqual(len(self.measurements()), 2)
        self.assertTrue(all(r.boot_id is None and r.seq is None for r in self.measurements()))

    def test_radio_and_independent_counters(self):
        self.client._process_measurement("TEST-1", self.data, self.received)
        self.client._process_status("TEST-1", {"mqtt_ok_cnt": 3,
            "mqtt_sessions_ok": 3, "data_publish_ok": 90,
            "data_publish_attempts": 100, "data_generated_count": 95,
            "reconnect_count": 2, "radio_valid": True, "rsrp_dbm": -115,
            "rsrq_db": -7, "rssnr_raw": 13, "sinr_db": 6, "boot_id": 7})
        with self.sessions() as db:
            row = db.query(NodoSalud).one()
            self.assertEqual((row.msg_tx, row.mqtt_exitos, row.reconexiones), (90, 3, 2))
            self.assertEqual((row.rsrp_dbm, row.rsrq_db, row.sinr_db), (-115, -7, 6))
            self.assertEqual(row.data_generated_count, 95)

    def test_ac_events_are_deduplicated_and_restore_resolves_loss(self):
        lost = {"event_id": "00000007-00000001", "boot_id": 7, "seq": 1,
            "ts_device": self.received.timestamp(), "time_valid": True,
            "fw": "1.1.0-obj4", "module": "SYSTEM", "code": "AC_LOST",
            "severity": "WARNING", "transition": "OCCURRENCE",
            "uptime_ms": 1000, "error": 259, "count": 1}
        restored = dict(lost, event_id="00000007-00000002", seq=2,
            code="AC_RESTORED", severity="INFO", uptime_ms=2000)
        self.client._process_alert("TEST-1", lost)
        self.client._process_alert("TEST-1", lost)
        self.client._process_alert("TEST-1", restored)
        with self.sessions() as db:
            rows = db.query(Evento).order_by(Evento.seq).all()
            self.assertEqual(len(rows), 2)
            self.assertFalse(rows[0].activo)
            self.assertEqual(rows[0].tipo, "ac_lost")
            self.assertFalse(rows[1].activo)
            self.assertEqual(rows[1].tipo, "ac_restored")
            self.assertEqual(rows[0].boot_id, 7)

    def test_legacy_session_count_is_not_misread_as_data_count(self):
        self.client._process_measurement("TEST-1", self.data, self.received)
        self.client._process_status("TEST-1", {"mqtt_ok_cnt": 12,
            "radio_valid": False, "rsrp_dbm": 0})
        with self.sessions() as db:
            row = db.query(NodoSalud).one()
            self.assertEqual(row.mqtt_exitos, 12)
            self.assertIsNone(row.msg_tx)
            self.assertIsNone(row.rsrp_dbm)

    def test_sequence_span_uses_separate_boots(self):
        rows = [SimpleNamespace(boot_id=b, seq=s, duplicate_count=0,
            identity_conflict_count=0) for b, s in [(1, 1), (1, 3), (2, 1)]]
        result = sequence_metrics(rows)
        self.assertEqual(result["porcentaje"], 75)
        self.assertEqual(result["huecos_observados"], 1)

    def test_sd_comparison_detects_missing_trailing_packets(self):
        sd = [dict(device_id="TEST-1", boot_id=1, seq=n, time_valid=False) for n in range(1, 11)]
        db = [dict(r, duplicate_count=0) for r in sd[:8]]
        result = reconcile(sd, db)
        self.assertEqual(result["pdr_eventual_percent"], 80)
        self.assertEqual(len(result["missing"]), 2)
        self.assertIsNone(result["pdr_live_percent"])

    def test_senml_identity_matches_sd_wrapper(self):
        packet = [{"bn": "urn:dev:serial:TEST-1:/", "bt": 0},
            {"n": "boot_id", "v": 7}, {"n": "seq", "v": 3}]
        self.assertEqual(flatten(packet), flatten({"payload": packet}))

    def test_callback_rejects_device_mismatch_and_preserves_matching_trace(self):
        packet = [{"bn": "urn:dev:serial:OTHER:/", "bt": 0},
            {"n": "boot_id", "v": 7}, {"n": "seq", "v": 3}]
        message = SimpleNamespace(topic="medidor/TEST-1/datos", retain=False,
            payload=json.dumps(packet).encode())
        self.client._on_message(None, None, message)
        self.assertEqual(len(self.measurements()), 0)
        packet[0]["bn"] = "urn:dev:serial:TEST-1:/"
        message.payload = json.dumps(packet).encode()
        self.client._on_message(None, None, message)
        row, = self.measurements()
        self.assertEqual((row.boot_id, row.seq), (7, 3))
        self.assertEqual(json.loads(row.raw_payload), packet)

    def test_one_line_sd_audit_is_one_measurement(self):
        packet = [{"bn": "urn:dev:serial:TEST-1:/"},
            {"n": "boot_id", "v": 7}, {"n": "seq", "v": 3}]
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "audit.jsonl"
            path.write_text(json.dumps(packet), encoding="utf-8")
            rows = load_records(path)
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]["seq"], 3)

    def test_invalid_identity_is_rejected(self):
        for values in ({"boot_id": 1}, {"boot_id": 1, "seq": 0},
                       {"boot_id": 1, "seq": 1.5}, {"boot_id": -1, "seq": 1}):
            with self.assertRaises(ValueError):
                trace_metadata(values, self.received)

    def test_invalid_clock_disables_live_pdr_but_not_completeness(self):
        source = dict(self.data, device_id="TEST-1", time_valid=False)
        received = dict(source, ts_server=self.received.isoformat())
        result = reconcile([source], [received])
        self.assertEqual(result["pdr_eventual_percent"], 100)
        self.assertIsNone(result["pdr_live_percent"])

    def test_source_without_identity_cannot_claim_full_pdr(self):
        result = reconcile([{"vrms": 120}], [])
        self.assertIsNone(result["pdr_eventual_percent"])
        self.assertEqual(result["sd_without_identity"], 1)

    def test_migration_is_additive_and_idempotent_on_existing_sqlite(self):
        engine = create_engine("sqlite:///:memory:")
        with engine.begin() as conn:
            conn.execute(text("CREATE TABLE dispositivos (id INTEGER PRIMARY KEY)"))
            conn.execute(text("CREATE TABLE mediciones (id INTEGER PRIMARY KEY, device_id VARCHAR(50))"))
            conn.execute(text("INSERT INTO mediciones VALUES (1, 'PRESERVED')"))
            conn.execute(text("CREATE TABLE nodo_salud (id INTEGER PRIMARY KEY)"))
        _aplicar_migraciones_incrementales(engine)
        _aplicar_migraciones_incrementales(engine)
        self.assertIn("seq", {r["name"] for r in inspect(engine).get_columns("mediciones")})
        with engine.connect() as conn:
            self.assertEqual(conn.execute(text("SELECT device_id FROM mediciones WHERE id=1")).scalar(), "PRESERVED")
        engine.dispose()


if __name__ == "__main__":
    unittest.main()
