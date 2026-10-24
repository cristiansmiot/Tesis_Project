"""Trace identity and timestamps for the characterization firmware."""
import hashlib
import json
import math
from datetime import datetime, timezone


def _uint(data, name, maximum=2**32 - 1):
    value = data.get(name)
    if value is None:
        return None
    if isinstance(value, bool):
        raise ValueError(f"{name} must be an unsigned integer")
    number = int(value)
    if number != float(value) or not 0 <= number <= maximum:
        raise ValueError(f"invalid {name}")
    return number


def trace_metadata(data, received_at=None):
    received_at = received_at or datetime.now(timezone.utc)
    boot_id, seq = _uint(data, "boot_id"), _uint(data, "seq")
    if (boot_id is None) != (seq is None) or seq == 0:
        raise ValueError("incomplete measurement identity")
    valid = data.get("time_valid")
    ts_device = None
    # Legacy bt is retained as a measurement timestamp but never invents
    # explicit clock validity or a trace identity for old packets.
    epoch = data.get("ts_device", data.get("_bt"))
    if epoch is not None and valid is not False:
        epoch = float(epoch)
        if math.isfinite(epoch) and 1577836800 <= epoch <= 4102444799:
            ts_device = datetime.fromtimestamp(epoch, tz=timezone.utc)
    if valid is True and ts_device is None:
        valid = False
    canonical = {k: v for k, v in data.items() if k != "_raw_payload"}
    digest = hashlib.sha256(json.dumps(canonical, sort_keys=True,
        separators=(",", ":"), allow_nan=False).encode()).hexdigest()
    return {
        "boot_id": boot_id, "seq": seq,
        "fw_version": str(data["fw"])[:20] if data.get("fw") else None,
        "uptime_ms": _uint(data, "uptime_ms", 2**63 - 1),
        "time_valid": valid if isinstance(valid, bool) else None,
        "ts_device": ts_device, "ts_server": received_at,
        "last_received_at": received_at,
        "calibration_status": _uint(data, "cal", 255),
        "raw_payload": data.get("_raw_payload") or json.dumps(canonical, sort_keys=True),
        "payload_sha256": digest,
    }


def sequence_metrics(rows):
    """Observed spans only; leading/trailing losses require the SD manifest."""
    boots = {}
    legacy = 0
    duplicates = conflicts = 0
    for row in rows:
        if row.boot_id is None or row.seq is None:
            legacy += 1
            continue
        boots.setdefault(row.boot_id, set()).add(row.seq)
        duplicates += row.duplicate_count or 0
        conflicts += row.identity_conflict_count or 0
    received = sum(len(seqs) for seqs in boots.values())
    expected = sum(max(seqs) - min(seqs) + 1 for seqs in boots.values())
    return {
        "porcentaje": round(100 * received / expected, 2) if expected else None,
        "mediciones_recibidas": received,
        "mediciones_esperadas": expected or None,
        "huecos_observados": expected - received,
        "duplicados": duplicates,
        "conflictos_identidad": conflicts,
        "registros_legacy": legacy,
        "intervalo_publish_s": 60,
        "metodo": "secuencias_por_boot",
        "alcance": "Solo entre primera y ultima secuencia recibidas de cada boot; PDR completo requiere conciliar la SD.",
        "boots": [{"boot_id": boot, "seq_min": min(seqs),
                   "seq_max": max(seqs), "recibidas": len(seqs)}
                  for boot, seqs in sorted(boots.items())],
    }
