#!/usr/bin/env python3
"""Build ArgusOS test media and drive its serial QEMU smoke test."""

from __future__ import annotations

import argparse
import os
import selectors
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import NoReturn, Sequence


MIB = 1024 * 1024
BOOT_MARKERS = (
    b"ARGUS_KERNEL_ONLINE",
    b"BOOT_SERVICES_EXITED",
    b"PMM_SELF_TEST_PASS",
    b"ACPI_MADT_ONLINE",
    b"PAGING_ONLINE",
    b"HEAP_SELF_TEST_PASS",
    b"MODULE_ABI_V1_ONLINE",
    b"RUST_MODULE_SELF_TEST_PASS",
    b"GDT_IDT_ONLINE",
    b"APIC_TIMER_TICK",
    b"KERNEL_SHELL_READY",
)
SHELL_PROBES = (
    (b"status\r", b"STATUS_OK"),
    (b"modules\r", b"MODULES_OK"),
    (b"alloc 4096\r", b"ALLOC_OK"),
)
OVMF_PAIRS = (
    ("/usr/share/OVMF/OVMF_CODE_4M.fd", "/usr/share/OVMF/OVMF_VARS_4M.fd"),
    ("/usr/share/OVMF/OVMF_CODE.fd", "/usr/share/OVMF/OVMF_VARS.fd"),
    ("/usr/share/edk2/ovmf/OVMF_CODE.fd", "/usr/share/edk2/ovmf/OVMF_VARS.fd"),
    ("/usr/share/edk2/x64/OVMF_CODE.fd", "/usr/share/edk2/x64/OVMF_VARS.fd"),
)


class ToolError(RuntimeError):
    """An actionable host-tool or smoke-test failure."""


def require_executable(name: str) -> str:
    executable = shutil.which(name)
    if not executable:
        raise ToolError(f"required executable not found: {name}")
    return executable


def run_checked(command: Sequence[str]) -> None:
    try:
        subprocess.run(command, check=True)
    except subprocess.CalledProcessError as error:
        raise ToolError(f"command failed ({error.returncode}): {' '.join(command)}") from error


def create_fat_image(efi_binary: Path, output: Path, size_mib: int) -> None:
    if not efi_binary.is_file():
        raise ToolError(f"EFI binary not found: {efi_binary}")
    if size_mib < 34:
        raise ToolError("FAT32 test images must be at least 34 MiB")

    mkfs_fat = require_executable("mkfs.fat")
    mmd = require_executable("mmd")
    mcopy = require_executable("mcopy")

    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("wb") as image:
        image.truncate(size_mib * MIB)

    run_checked((mkfs_fat, "-F", "32", str(output)))
    run_checked((mmd, "-i", str(output), "::/EFI"))
    run_checked((mmd, "-i", str(output), "::/EFI/BOOT"))
    run_checked(
        (mcopy, "-i", str(output), str(efi_binary), "::/EFI/BOOT/BOOTX64.EFI")
    )


def locate_ovmf(code_override: Path | None, vars_override: Path | None) -> tuple[Path, Path]:
    if (code_override is None) != (vars_override is None):
        raise ToolError("--ovmf-code and --ovmf-vars must be provided together")
    if code_override is not None and vars_override is not None:
        if not code_override.is_file() or not vars_override.is_file():
            raise ToolError("the requested OVMF code/variable-store pair is unreadable")
        return code_override, vars_override

    for code_name, vars_name in OVMF_PAIRS:
        code = Path(code_name)
        variables = Path(vars_name)
        if code.is_file() and variables.is_file():
            return code, variables
    raise ToolError("no supported OVMF code/variable-store pair was found")


class SerialSession:
    def __init__(self, command: Sequence[str]) -> None:
        self.process = subprocess.Popen(
            command,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            bufsize=0,
        )
        if self.process.stdin is None or self.process.stdout is None:
            raise ToolError("failed to open QEMU serial pipes")
        self.selector = selectors.DefaultSelector()
        self.selector.register(self.process.stdout, selectors.EVENT_READ)
        self.transcript = bytearray()
        self.cursor = 0

    def _read_chunk(self, timeout: float) -> None:
        events = self.selector.select(timeout)
        if not events:
            if self.process.poll() is not None:
                raise ToolError(f"QEMU exited early with status {self.process.returncode}")
            return

        chunk = os.read(self.process.stdout.fileno(), 4096)
        if not chunk:
            raise ToolError("QEMU closed its serial output unexpectedly")
        self.transcript.extend(chunk)
        sys.stdout.buffer.write(chunk)
        sys.stdout.buffer.flush()

    def expect(self, marker: bytes, timeout: float) -> None:
        deadline = time.monotonic() + timeout
        while True:
            found = self.transcript.find(marker, self.cursor)
            if found >= 0:
                self.cursor = found + len(marker)
                return
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                readable = marker.decode("ascii", errors="replace")
                raise ToolError(f"timed out waiting for serial marker: {readable}")
            self._read_chunk(min(remaining, 0.25))

    def send(self, data: bytes) -> None:
        try:
            self.process.stdin.write(data)
            self.process.stdin.flush()
        except BrokenPipeError as error:
            raise ToolError("QEMU closed its serial input unexpectedly") from error

    def stop(self) -> None:
        if self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=3)
        self.selector.close()


def qemu_command(
    qemu: str,
    memory: str,
    ovmf_code: Path,
    ovmf_vars: Path,
    image: Path,
) -> tuple[str, ...]:
    return (
        qemu,
        "-display",
        "none",
        "-serial",
        "stdio",
        "-machine",
        "accel=tcg",
        "-m",
        memory,
        "-drive",
        f"if=pflash,format=raw,readonly=on,file={ovmf_code}",
        "-drive",
        f"if=pflash,format=raw,file={ovmf_vars}",
        "-drive",
        f"if=virtio,format=raw,file={image}",
        "-no-reboot",
        "-monitor",
        "none",
        "-nodefaults",
    )


def run_smoke(args: argparse.Namespace) -> None:
    qemu = require_executable(args.qemu)
    ovmf_code, ovmf_template = locate_ovmf(args.ovmf_code, args.ovmf_vars)
    temporary = tempfile.TemporaryDirectory(prefix="argusos-smoke-")
    work = Path(temporary.name)
    image = work / "efi-test.img"
    ovmf_vars = work / "OVMF_VARS.fd"
    session: SerialSession | None = None

    try:
        create_fat_image(args.efi, image, args.image_size)
        shutil.copyfile(ovmf_template, ovmf_vars)
        session = SerialSession(
            qemu_command(qemu, args.memory, ovmf_code, ovmf_vars, image)
        )

        session.expect(b"argus64> ", args.timeout)
        session.send(b"boot\r")
        for marker in BOOT_MARKERS:
            session.expect(marker, args.timeout)
        session.expect(b"argus-kernel> ", args.timeout)

        for command, marker in SHELL_PROBES:
            session.send(command)
            session.expect(marker, args.timeout)
            session.expect(b"argus-kernel> ", args.timeout)
    finally:
        if session is not None:
            session.stop()
            args.log.parent.mkdir(parents=True, exist_ok=True)
            args.log.write_bytes(session.transcript)
        temporary.cleanup()

    print(f"\nArgusOS smoke test passed; transcript: {args.log}")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="operation", required=True)

    image = subparsers.add_parser("image", help="create a FAT32 UEFI test image")
    image.add_argument("--efi", type=Path, required=True, help="BOOTX64.EFI input")
    image.add_argument("--output", type=Path, required=True, help="disk image output")
    image.add_argument("--size", type=int, default=64, help="image size in MiB")

    smoke = subparsers.add_parser("smoke", help="boot and probe the native shell")
    smoke.add_argument("--efi", type=Path, required=True, help="BOOTX64.EFI input")
    smoke.add_argument("--memory", default="1G", help="QEMU guest memory")
    smoke.add_argument("--image-size", type=int, default=64, help="FAT image size in MiB")
    smoke.add_argument("--timeout", type=float, default=25.0, help="per-marker timeout")
    smoke.add_argument("--log", type=Path, default=Path("build/qemu-smoke.log"))
    smoke.add_argument("--qemu", default="qemu-system-x86_64")
    smoke.add_argument("--ovmf-code", type=Path)
    smoke.add_argument("--ovmf-vars", type=Path)
    return parser


def fail(message: str) -> NoReturn:
    print(f"error: {message}", file=sys.stderr)
    raise SystemExit(1)


def main() -> None:
    args = build_parser().parse_args()
    try:
        if args.operation == "image":
            create_fat_image(args.efi, args.output, args.size)
            print(f"Created UEFI test image: {args.output}")
        else:
            run_smoke(args)
    except (OSError, ToolError) as error:
        fail(str(error))


if __name__ == "__main__":
    main()
