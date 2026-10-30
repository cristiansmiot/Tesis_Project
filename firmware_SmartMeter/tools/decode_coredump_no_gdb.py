"""Extract the essential ESP32-S3 panic data without launching GDB.

This helper is intended for Windows hosts where the Xtensa GDB executable
cannot start.  It converts an ESP-IDF raw coredump partition to ELF, reads the
panic note and exception registers, and resolves the current PC with pyelftools.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from construct import Int32ul
from elftools.elf.elffile import ELFFile
from esp_coredump.coredump import CoreDump
from esp_coredump.corefile.elf import ESPCoreDumpElfFile, ElfFile, EspTaskStatus
from esp_coredump.corefile.xtensa import PrStatus, REG_AR_START_IDX, REG_PC_IDX


def resolve_address(elf_path: Path, address: int) -> dict:
    result = {"address": f"0x{address:08x}", "symbol": None, "file": None, "line": None}
    with elf_path.open("rb") as stream:
        elf = ELFFile(stream)
        best = None
        for section_name in (".symtab", ".dynsym"):
            section = elf.get_section_by_name(section_name)
            if section is None:
                continue
            for symbol in section.iter_symbols():
                start = int(symbol["st_value"])
                size = int(symbol["st_size"])
                if start <= address and (size == 0 or address < start + size):
                    distance = address - start
                    if best is None or distance < best[0]:
                        best = (distance, symbol.name)
        if best:
            result["symbol"] = best[1]

        if elf.has_dwarf_info():
            dwarf = elf.get_dwarf_info()
            for cu in dwarf.iter_CUs():
                line_program = dwarf.line_program_for_CU(cu)
                if line_program is None:
                    continue
                previous = None
                for entry in line_program.get_entries():
                    if entry.state is None:
                        continue
                    state = entry.state
                    if previous is not None and previous.address <= address < state.address:
                        file_entry = line_program["file_entry"][previous.file - 1]
                        directory = b""
                        if file_entry.dir_index:
                            directory = line_program["include_directory"][file_entry.dir_index - 1]
                        filename = file_entry.name
                        result["file"] = str(Path(directory.decode(errors="replace")) / filename.decode(errors="replace"))
                        result["line"] = previous.line
                        return result
                    previous = state
    return result


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--raw", required=True, type=Path)
    parser.add_argument("--firmware", required=True, type=Path)
    parser.add_argument("--core-elf", required=True, type=Path)
    parser.add_argument("--json", required=True, type=Path)
    args = parser.parse_args()

    dump = CoreDump(
        chip="esp32s3",
        core=str(args.raw),
        core_format="raw",
        prog=str(args.firmware),
        save_core=str(args.core_elf),
    )
    program = ESPCoreDumpElfFile(str(args.firmware))
    info = dump.get_core_header_info_dict(e_machine=program.e_machine)
    core = ESPCoreDumpElfFile(info["core_elf_path"])

    report = {
        "target": info.get("target"),
        "chip_revision": info.get("chip_rev"),
        "panic_reason": None,
        "crashed_task_tcb": None,
        "exception": {},
        "tasks": [],
        "current_registers": {},
    }

    core_notes = []
    for segment in core.note_segments:
        for note in segment.note_secs:
            if note.type == ESPCoreDumpElfFile.PT_ESP_PANIC_DETAILS:
                report["panic_reason"] = note.desc.rstrip(b"\x00").decode(errors="replace")
            elif note.type == ESPCoreDumpElfFile.PT_ESP_EXTRA_INFO:
                values = Int32ul[len(note.desc) // 4].parse(note.desc)
                if values:
                    report["crashed_task_tcb"] = f"0x{values[0]:08x}"
                registers = {values[i]: values[i + 1] for i in range(1, len(values) - 1, 2)}
                cause = registers.get(0)
                report["exception"] = {
                    "cause": cause,
                    "address": f"0x{registers.get(1, 0):08x}",
                    "register_ids": {str(k): f"0x{v:08x}" for k, v in registers.items()},
                }
            elif note.type == ESPCoreDumpElfFile.PT_ESP_TASK_INFO:
                task = EspTaskStatus.parse(note.desc)
                report["tasks"].append(
                    {
                        "index": task.task_index,
                        "flags": task.task_flags,
                        "tcb": f"0x{task.task_tcb_addr:08x}",
                        "stack_start": f"0x{task.task_stack_start:08x}",
                        "stack_len": task.task_stack_len,
                    }
                )
            elif note.name == b"CORE" and note.type == ElfFile.PT_LOAD:
                core_notes.append(note)

    if core_notes:
        desc = core_notes[0].desc
        regs = Int32ul[(len(desc) - PrStatus.sizeof()) // 4].parse(desc[PrStatus.sizeof() :])
        pc = int(regs[REG_PC_IDX])
        a0 = int(regs[REG_AR_START_IDX])
        a1 = int(regs[REG_AR_START_IDX + 1])
        report["current_registers"] = {
            "pc": resolve_address(args.firmware, pc),
            "a0": resolve_address(args.firmware, a0),
            "a1": f"0x{a1:08x}",
        }

    args.json.write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
    print(json.dumps(report, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
