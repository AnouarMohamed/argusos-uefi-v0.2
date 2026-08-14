#!/usr/bin/env python3
"""Build ArgusOS test media and drive its serial QEMU smoke test."""

from __future__ import annotations

import argparse
import json
import os
import selectors
import shutil
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import NoReturn, Sequence


MIB = 1024 * 1024
PROJECT_ROOT = Path(__file__).resolve().parent.parent
FAT32_PROBE_FILE = PROJECT_ROOT / "assets" / "HELLO.TXT"
BOOT_MARKERS = (
    b"ARGUS_KERNEL_ONLINE",
    b"BOOT_SERVICES_EXITED",
    b"PMM_SELF_TEST_PASS",
    b"ACPI_MADT_ONLINE",
    b"PAGING_ONLINE",
    b"STACK_GUARD_ONLINE",
    b"HEAP_SELF_TEST_PASS",
    b"ALLOCATOR_HARDENING_PASS",
    b"MODULE_ABI_V1_ONLINE",
    b"RUST_MODULE_SELF_TEST_PASS",
    b"RAMFS_ABI_V1_ONLINE",
    b"RUST_RAMFS_SELF_TEST_PASS",
    b"PCI_DISCOVERY_ONLINE",
    b"PCI_SELF_TEST_PASS",
    b"AHCI_SATA_ONLINE",
    b"AHCI_IDENTIFY_PASS",
    b"BLOCK_DEVICE_ONLINE",
    b"BLOCK_DEVICE_SELF_TEST_PASS",
    b"FAT32_ABI_V1_ONLINE",
    b"RUST_FAT32_SELF_TEST_PASS",
    b"GDT_IDT_ONLINE",
    b"DOUBLE_FAULT_IST_ONLINE",
    b"APIC_TIMER_TICK",
    b"DESKTOP_UI_ONLINE",
    b"PS2_IRQ_ONLINE",
    b"PS2_MOUSE_IRQ_ONLINE",
    b"KERNEL_SHELL_READY",
)
SHELL_PROBES = (
    (b"status\r", b"STATUS_OK"),
    (b"desktop\r", b"DESKTOP_UI_REDRAWN"),
    (b"modules\r", b"MODULES_OK"),
    (b"alloc 4096\r", b"ALLOC_OK"),
    (b"memtest\r", b"ALLOCATOR_HARDENING_PASS"),
    (b"input\r", b"PS/2 mode: I/O APIC IRQ"),
    (b"fs\r", b"RAMFS_STATUS_OK"),
    (b"ls\r", b"RAMFS_LIST_OK"),
    (b"cat /README\r", b"RAMFS_CAT_OK"),
    (b"write /qemu.txt Rust RAMFS round trip\r", b"RAMFS_WRITE_OK"),
    (b"cat /qemu.txt\r", b"Rust RAMFS round trip"),
    (b"rm /qemu.txt\r", b"RAMFS_REMOVE_OK"),
    (b"cat /qemu.txt\r", b"RAMFS error: not found"),
    (b"pci\r", b"PCI_STATUS_OK"),
    (b"ahci\r", b"AHCI_STATUS_OK"),
    (b"disks\r", b"BLOCK_STATUS_OK"),
    (b"fatinfo\r", b"FAT32_STATUS_OK"),
    (b"fatls\r", b"FAT32_LIST_OK"),
    (b"fatcat /hello.txt\r", b"FAT32_CAT_OK"),
    (b"fatcat /MISSING.TXT\r", b"FAT32 error: not found"),
)
FAULT_CASES = {
    "breakpoint": (
        b"bootfault\r",
        (
            b"STACK_GUARD_ONLINE",
            b"DOUBLE_FAULT_IST_ONLINE",
            b"EXCEPTION_SELF_TEST_BEGIN",
            b"KERNEL_EXCEPTION",
            b"Vector: 3",
        ),
    ),
    "guard": (
        b"bootguard\r",
        (
            b"STACK_GUARD_ONLINE",
            b"DOUBLE_FAULT_IST_ONLINE",
            b"STACK_GUARD_SELF_TEST_BEGIN",
            b"KERNEL_EXCEPTION",
            b"Vector: 14",
            b"STACK_GUARD_FAULT_CAUGHT",
        ),
    ),
    "double": (
        b"bootdouble\r",
        (
            b"STACK_GUARD_ONLINE",
            b"DOUBLE_FAULT_IST_ONLINE",
            b"DOUBLE_FAULT_SELF_TEST_BEGIN",
            b"KERNEL_EXCEPTION",
            b"Vector: 8",
            b"DOUBLE_FAULT_IST_ACTIVE",
        ),
    ),
}
OVMF_PAIRS = (
    ("/usr/share/OVMF/OVMF_CODE_4M.fd", "/usr/share/OVMF/OVMF_VARS_4M.fd"),
    ("/usr/share/OVMF/OVMF_CODE.fd", "/usr/share/OVMF/OVMF_VARS.fd"),
    ("/usr/share/edk2/ovmf/OVMF_CODE.fd", "/usr/share/edk2/ovmf/OVMF_VARS.fd"),
    ("/usr/share/edk2/x64/OVMF_CODE.fd", "/usr/share/edk2/x64/OVMF_VARS.fd"),
)

DESKTOP_PALETTE = {
    "field olive": bytes((0x4C, 0x51, 0x48)),
    "warm chrome": bytes((0xB8, 0xB4, 0xA5)),
    "terminal ink": bytes((0x17, 0x1A, 0x17)),
    "slate title": bytes((0x4E, 0x58, 0x69)),
}


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


def verify_desktop_capture(path: Path) -> None:
    capture = path.read_bytes()
    if not capture.startswith(b"P6\n"):
        raise ToolError("QEMU framebuffer capture is not a binary PPM image")
    for name, rgb in DESKTOP_PALETTE.items():
        if rgb not in capture:
            raise ToolError(f"framebuffer capture is missing desktop color: {name}")


def create_fat_image(efi_binary: Path, output: Path, size_mib: int) -> None:
    if not efi_binary.is_file():
        raise ToolError(f"EFI binary not found: {efi_binary}")
    if size_mib < 34:
        raise ToolError("FAT32 test images must be at least 34 MiB")
    if not FAT32_PROBE_FILE.is_file():
        raise ToolError(f"FAT32 probe file not found: {FAT32_PROBE_FILE}")

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
    run_checked(
        (mcopy, "-i", str(output), str(FAT32_PROBE_FILE), "::/HELLO.TXT")
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


class QmpClient:
    def __init__(self, connection: socket.socket) -> None:
        self.connection = connection
        self.stream = connection.makefile("rwb", buffering=0)
        greeting = self._receive()
        if "QMP" not in greeting:
            raise ToolError("QEMU control socket returned an invalid greeting")
        self.execute("qmp_capabilities")

    @classmethod
    def connect(cls, path: Path, timeout: float) -> "QmpClient":
        deadline = time.monotonic() + timeout
        while True:
            connection = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            try:
                connection.connect(str(path))
                connection.settimeout(timeout)
                try:
                    return cls(connection)
                except Exception:
                    connection.close()
                    raise
            except (FileNotFoundError, ConnectionRefusedError):
                connection.close()
                if time.monotonic() >= deadline:
                    raise ToolError("timed out connecting to the QEMU control socket")
                time.sleep(0.05)

    def _receive(self) -> dict[str, object]:
        line = self.stream.readline()
        if not line:
            raise ToolError("QEMU closed its control socket unexpectedly")
        try:
            response = json.loads(line)
        except json.JSONDecodeError as error:
            raise ToolError("QEMU returned invalid JSON on its control socket") from error
        if not isinstance(response, dict):
            raise ToolError("QEMU returned a malformed control response")
        return response

    def execute(self, command: str, arguments: dict[str, object] | None = None) -> object:
        request: dict[str, object] = {"execute": command}
        if arguments is not None:
            request["arguments"] = arguments
        self.stream.write(json.dumps(request).encode("utf-8") + b"\r\n")
        while True:
            response = self._receive()
            if "error" in response:
                raise ToolError(f"QEMU control command failed: {response['error']}")
            if "return" in response:
                return response["return"]

    def send_key(self, key: str) -> None:
        self.execute(
            "human-monitor-command",
            {"command-line": f"sendkey {key}"},
        )

    def move_pointer(self, dx: int, dy: int) -> None:
        events: list[dict[str, object]] = []
        if dx:
            events.append({"type": "rel", "data": {"axis": "x", "value": dx}})
        if dy:
            events.append({"type": "rel", "data": {"axis": "y", "value": dy}})
        if events:
            self.execute("input-send-event", {"events": events})

    def pointer_button(self, button: str, down: bool) -> None:
        self.execute(
            "input-send-event",
            {"events": [{"type": "btn", "data": {"button": button, "down": down}}]},
        )

    def close(self) -> None:
        self.stream.close()
        self.connection.close()


def qemu_command(
    qemu: str,
    memory: str,
    ovmf_code: Path,
    ovmf_vars: Path,
    image: Path,
    qmp_socket: Path | None = None,
) -> tuple[str, ...]:
    command = [
        qemu,
        "-display",
        "none",
        "-serial",
        "stdio",
        "-machine",
        "q35,accel=tcg",
        "-m",
        memory,
        "-drive",
        f"if=pflash,format=raw,readonly=on,file={ovmf_code}",
        "-drive",
        f"if=pflash,format=raw,file={ovmf_vars}",
        "-drive",
        f"if=ide,format=raw,file={image}",
        "-vga",
        "std",
        "-no-reboot",
        "-monitor",
        "none",
        "-nodefaults",
    ]
    if qmp_socket is not None:
        command.extend(("-qmp", f"unix:{qmp_socket},server=on,wait=off"))
    return tuple(command)


def run_smoke(args: argparse.Namespace) -> None:
    qemu = require_executable(args.qemu)
    ovmf_code, ovmf_template = locate_ovmf(args.ovmf_code, args.ovmf_vars)
    temporary = tempfile.TemporaryDirectory(prefix="argusos-smoke-")
    work = Path(temporary.name)
    image = work / "efi-test.img"
    ovmf_vars = work / "OVMF_VARS.fd"
    qmp_socket = work / "qmp.sock"
    session: SerialSession | None = None
    qmp: QmpClient | None = None

    try:
        create_fat_image(args.efi, image, args.image_size)
        shutil.copyfile(ovmf_template, ovmf_vars)
        session = SerialSession(
            qemu_command(qemu, args.memory, ovmf_code, ovmf_vars, image, qmp_socket)
        )
        qmp = QmpClient.connect(qmp_socket, args.timeout)

        session.expect(b"argus64> ", args.timeout)
        session.send(b"boot\r")
        for marker in BOOT_MARKERS:
            session.expect(marker, args.timeout)
        session.expect(b"argus-kernel> ", args.timeout)

        args.screenshot.parent.mkdir(parents=True, exist_ok=True)
        qmp.execute("screendump", {"filename": str(args.screenshot.resolve())})
        if not args.screenshot.is_file():
            raise ToolError("QEMU did not create the framebuffer screenshot")
        verify_desktop_capture(args.screenshot)

        for command, marker in SHELL_PROBES:
            session.send(command)
            session.expect(marker, args.timeout)
            session.expect(b"argus-kernel> ", args.timeout)

        for key in "irqtest":
            qmp.send_key(key)
        qmp.send_key("ret")
        session.expect(b"PS2_IRQ_INPUT_OK", args.timeout)
        session.expect(b"argus-kernel> ", args.timeout)

        qmp.move_pointer(0, -280)
        time.sleep(0.05)
        qmp.pointer_button("left", True)
        time.sleep(0.05)
        qmp.move_pointer(120, 60)
        time.sleep(0.05)
        qmp.pointer_button("left", False)
        time.sleep(0.05)
        session.send(b"ui\r")
        session.expect(b"DESKTOP_DRAG_OK", args.timeout)
        session.expect(b"argus-kernel> ", args.timeout)

        args.drag_screenshot.parent.mkdir(parents=True, exist_ok=True)
        qmp.execute("screendump", {"filename": str(args.drag_screenshot.resolve())})
        if not args.drag_screenshot.is_file():
            raise ToolError("QEMU did not create the dragged-window screenshot")
        verify_desktop_capture(args.drag_screenshot)
    finally:
        try:
            if qmp is not None:
                qmp.close()
        finally:
            if session is not None:
                session.stop()
                args.log.parent.mkdir(parents=True, exist_ok=True)
                args.log.write_bytes(session.transcript)
        temporary.cleanup()

    print(
        f"\nArgusOS smoke test passed; transcript: {args.log}; "
        f"framebuffer: {args.screenshot}; dragged: {args.drag_screenshot}"
    )


def run_fault(args: argparse.Namespace) -> None:
    qemu = require_executable(args.qemu)
    ovmf_code, ovmf_template = locate_ovmf(args.ovmf_code, args.ovmf_vars)
    temporary = tempfile.TemporaryDirectory(prefix="argusos-fault-")
    work = Path(temporary.name)
    image = work / "efi-test.img"
    ovmf_vars = work / "OVMF_VARS.fd"
    session: SerialSession | None = None
    boot_command, markers = FAULT_CASES[args.case]

    try:
        create_fat_image(args.efi, image, args.image_size)
        shutil.copyfile(ovmf_template, ovmf_vars)
        session = SerialSession(
            qemu_command(qemu, args.memory, ovmf_code, ovmf_vars, image)
        )
        session.expect(b"argus64> ", args.timeout)
        session.send(boot_command)
        for marker in markers:
            session.expect(marker, args.timeout)
    finally:
        if session is not None:
            session.stop()
            args.log.parent.mkdir(parents=True, exist_ok=True)
            args.log.write_bytes(session.transcript)
        temporary.cleanup()

    print(f"\nArgusOS {args.case} fault test passed; transcript: {args.log}")


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
    smoke.add_argument(
        "--screenshot",
        type=Path,
        default=Path("build/qemu-desktop.ppm"),
        help="framebuffer capture after the kernel shell becomes ready",
    )
    smoke.add_argument(
        "--drag-screenshot",
        type=Path,
        default=Path("build/qemu-desktop-dragged.ppm"),
        help="framebuffer capture after the pointer drag probe",
    )
    smoke.add_argument("--qemu", default="qemu-system-x86_64")
    smoke.add_argument("--ovmf-code", type=Path)
    smoke.add_argument("--ovmf-vars", type=Path)

    fault = subparsers.add_parser("fault", help="require a kernel fault diagnostic")
    fault.add_argument("--efi", type=Path, required=True, help="BOOTX64.EFI input")
    fault.add_argument("--case", choices=FAULT_CASES, required=True)
    fault.add_argument("--memory", default="256M", help="QEMU guest memory")
    fault.add_argument("--image-size", type=int, default=64, help="FAT image size in MiB")
    fault.add_argument("--timeout", type=float, default=25.0, help="per-marker timeout")
    fault.add_argument("--log", type=Path, default=Path("build/qemu-fault.log"))
    fault.add_argument("--qemu", default="qemu-system-x86_64")
    fault.add_argument("--ovmf-code", type=Path)
    fault.add_argument("--ovmf-vars", type=Path)
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
        elif args.operation == "smoke":
            run_smoke(args)
        else:
            run_fault(args)
    except (OSError, ToolError) as error:
        fail(str(error))


if __name__ == "__main__":
    main()
