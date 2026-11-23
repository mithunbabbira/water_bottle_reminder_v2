#!/usr/bin/env python3
"""Bounded, secret-safe protocol-v2 smoke tests. Never opens a port on import.

Use --help; see docs/HARDWARE_TESTING.md before connecting real hardware.
Requires Python 3.10+. pyserial is imported only when actually connecting.
The protocol helpers and suite are also used by ble_smoke.py.
"""
from __future__ import annotations

import argparse
import asyncio
from collections import deque
from datetime import datetime, timezone
import json
import math
from pathlib import Path
import re
import sys
import time
from typing import Any

MAX_LINE = 4096
MAX_PENDING = 256
SETTING_RANGES = {
    "start_min": (0, 1439), "end_min": (0, 1439), "interval_min": (1, 240),
    "beep_type": (0, 3), "color": (0, 0xFFFFFF),
    "reminder_color": (0, 0xFFFFFF), "missing_color": (0, 0xFFFFFF),
    "missing_sec": (10, 3600), "beep_delay_sec": (0, 30),
    "drink_grams": (5, 500), "settle_ms": (500, 5000), "tz_min": (-720, 840),
}
SETTING_KEYS = set(SETTING_RANGES) | {"dnd", "presence_enabled"}
NETWORK_KEYS = {"net_count", "sta_ssid", "sta_pass", "ap_ssid", "ap_pass", "ap_behavior"}
NETWORK_KEYS |= {f"wifi_{kind}{i}" for i in range(4) for kind in ("s", "p")}
REQUIRED_NVS = (SETTING_KEYS | NETWORK_KEYS | {
    "schema", "tare_offset", "cal_factor", "baseline_weight", "daily_total",
    "last_day", "ble_mac", "ble_irk", "classic_mac", "classic_name",
}) - {"sta_ssid", "sta_pass"}  # Firmware may expose primary-network aliases too.
STABLE_NVS = SETTING_KEYS | NETWORK_KEYS | {"tare_offset", "cal_factor", "ble_irk", "classic_mac", "classic_name"}


class SmokeFailure(Exception):
    """Only fixed, credential-free diagnostic strings belong in this exception."""


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SmokeFailure(message)


def sensitive_key(key: str) -> bool:
    key = key.lower()
    compact = re.sub(r"[^a-z0-9]", "", key)
    return (any(part in compact for part in ("password", "passwd", "passphrase", "irk", "secret"))
            or compact.endswith("pass") or bool(re.fullmatch(r"wifi_?p\d+", key)))


def sanitize(value: Any) -> Any:
    """Sanitize recursively, including NVS {key: ..., value: ...} envelopes.

    No raw input, outgoing command, or device error text is included in artifacts.
    Keep only the documented credential presence markers, never their content.
    """
    if isinstance(value, dict):
        result = {}
        nvs_secret = isinstance(value.get("key"), str) and sensitive_key(value["key"])
        for key, item in value.items():
            if sensitive_key(str(key)) or (key == "value" and nvs_secret):
                result[key] = item if item is None or item in ("set", "empty") else "[REDACTED]"
            elif key in {"error", "wifi_error", "message"} and item:
                result[key] = "[device text omitted]"
            elif key in {"phone_mac", "ble_mac", "classic_mac", "app_peer_mac"}:
                result[key] = "[REDACTED]" if item else item
            elif key == "value" and value.get("key") in {"ble_mac", "classic_mac"}:
                result[key] = "[REDACTED]" if item else item
            else:
                result[key] = sanitize(item)
        return result
    if isinstance(value, (list, tuple)):
        return [sanitize(item) for item in value]
    return value


def reject_constant(_: str) -> None:
    raise ValueError("Non-finite JSON number")


class JsonLineDecoder:
    """Incremental UTF-8/newline decoder with bounded memory and boot-log filtering."""

    def __init__(self, max_line: int = MAX_LINE):
        self.max_line = max_line
        self.buffer = bytearray()
        self.discarding = False
        self.stats = {"frames": 0, "ignored_lines": 0, "invalid_json": 0, "oversize_lines": 0}

    def feed(self, chunk: bytes) -> list[dict]:
        messages = []
        for byte in chunk:
            if byte == 10:
                if not self.discarding and self.buffer:
                    raw = bytes(self.buffer).strip()
                    if not raw.startswith(b"{"):
                        self.stats["ignored_lines"] += 1
                    else:
                        try:
                            obj = json.loads(raw.decode("utf-8"), parse_constant=reject_constant)
                            if not isinstance(obj, dict):
                                raise ValueError("Not an object")
                            messages.append(obj)
                            self.stats["frames"] += 1
                        except (ValueError, UnicodeError, RecursionError):
                            self.stats["invalid_json"] += 1
                self.buffer.clear()
                self.discarding = False
            elif byte != 13 and not self.discarding:
                if len(self.buffer) >= self.max_line:
                    self.buffer.clear()
                    self.discarding = True
                    self.stats["oversize_lines"] += 1
                else:
                    self.buffer.append(byte)
        return messages


def encode_command(request_id: int, command: str, **fields: Any) -> bytes:
    return (json.dumps({"id": request_id, "cmd": command, **fields},
                       ensure_ascii=False, separators=(",", ":"), allow_nan=False) + "\n").encode("utf-8")


def chunks(data: bytes, size: int = 20) -> list[bytes]:
    if size < 1:
        raise ValueError("Chunk size must be positive")
    return [data[i:i + size] for i in range(0, len(data), size)]


def validate_status(message: dict) -> None:
    require(message.get("type") == "status", "Expected a status response")
    require(type(message.get("protocol")) is int and message["protocol"] == 2,
            "Firmware must report protocol 2")
    require(message.get("ready") is True, "Firmware storage is not ready")
    settings = message.get("settings")
    require(isinstance(settings, dict) and SETTING_KEYS <= settings.keys(),
            "Status is missing settings fields")
    for key, (low, high) in SETTING_RANGES.items():
        number = settings[key]
        require(type(number) is int and low <= number <= high,
                "Status contains an invalid numeric setting")
    require(settings["tz_min"] % 15 == 0, "Status timezone is not a 15-minute increment")
    for key in ("dnd", "presence_enabled"):
        require(type(settings[key]) is bool, "Status contains an invalid boolean setting")
    networks = message.get("networks")
    require(isinstance(networks, list) and len(networks) <= 4, "Status networks are invalid")
    for network in networks:
        require(isinstance(network, dict) and isinstance(network.get("ssid"), str)
                and type(network.get("secured")) is bool, "Status network entry is invalid")
    check_no_secrets(message)


def check_no_secrets(value: Any) -> None:
    """Assert firmware readback itself is redacted, before defensive sanitization."""
    if isinstance(value, dict):
        nvs_secret = isinstance(value.get("key"), str) and sensitive_key(value["key"])
        for key, item in value.items():
            if sensitive_key(str(key)) or (key == "value" and nvs_secret):
                require(item is None or item in ("set", "empty"),
                        "Firmware exposed a credential instead of a presence marker")
            else:
                check_no_secrets(item)
    elif isinstance(value, list):
        for item in value:
            check_no_secrets(item)


def invalid_settings(snapshot: dict) -> list[tuple[str, dict]]:
    """Include a valid changed field to detect partial application of invalid requests."""
    cases = []
    for key, (low, high) in SETTING_RANGES.items():
        for label, value in (("type", "invalid"), ("low", low - 1), ("high", high + 1),
                             ("null", None)):
            cases.append((f"settings.reject.{key}.{label}", {"dnd": not snapshot["dnd"], key: value}))
    for key in ("dnd", "presence_enabled"):
        for label, value in (("integer", 1), ("string", "true"), ("null", None)):
            cases.append((f"settings.reject.{key}.{label}",
                          {"color": snapshot["color"] ^ 0x010101, key: value}))
    for label, fields in (
        ("numeric_bool", {"interval_min": True}),
        ("fraction", {"interval_min": 1.5}),
        ("timezone_step", {"tz_min": 1}),
    ):
        cases.append((f"settings.reject.{label}", {"dnd": not snapshot["dnd"], **fields}))
    return cases


class ProtocolClient:
    """One request at a time, except the deliberate two-command framing test.

    ACK and NVS frames carry IDs. Protocol-v2 status does not: accept only the
    first status *after* the matching ACK, on an otherwise exclusive connection.
    """

    def __init__(self, transport: Any, timeout: float = 12, stream_timeout: float = 45):
        self.transport = transport
        self.timeout = timeout
        self.stream_timeout = stream_timeout
        self.decoder = JsonLineDecoder()
        self.pending: deque[dict] = deque()
        self.counter = 1000

    def new_id(self) -> int:
        self.counter += 1
        return self.counter

    async def write(self, data: bytes) -> None:
        try:
            await asyncio.wait_for(self.transport.write(data), self.timeout)
        except asyncio.TimeoutError:
            raise SmokeFailure("Timed out writing a command") from None

    async def next_message(self, deadline: float) -> dict:
        while True:
            if time.monotonic() >= deadline:
                raise SmokeFailure("Timed out waiting for a complete protocol response")
            if self.pending:
                return self.pending.popleft()
            remaining = deadline - time.monotonic()
            try:
                data = await asyncio.wait_for(self.transport.read(min(0.25, remaining)), remaining)
            except asyncio.TimeoutError:
                raise SmokeFailure("Timed out waiting for transport data") from None
            self.pending.extend(self.decoder.feed(data))
            require(len(self.pending) <= MAX_PENDING, "Too many unconsumed protocol messages")

    async def ack(self, request_id: int, expected_ok: bool, deadline: float) -> dict:
        while True:
            message = await self.next_message(deadline)
            if message.get("type") == "ack" and message.get("id") == request_id:
                require(type(message.get("id")) is int, "ACK ID has the wrong type")
                require(message.get("ok") is expected_ok, "ACK result differed from the expected result")
                if not expected_ok:
                    require(isinstance(message.get("error"), str) and bool(message["error"]),
                            "Rejected command has no error explanation")
                return message

    async def status_after_ack(self, request_id: int, deadline: float) -> dict:
        while True:
            message = await self.next_message(deadline)
            if message.get("type") == "status":
                # Allow an explicit ID if a compatible newer build adds one.
                if "id" in message and message["id"] != request_id:
                    continue
                validate_status(message)
                return message

    async def request(self, command: str, *, ok: bool = True, response: bool = False,
                      **fields: Any) -> dict:
        request_id = self.new_id()
        deadline = time.monotonic() + self.timeout
        await self.write(encode_command(request_id, command, **fields))
        reply = await self.ack(request_id, ok, deadline)
        return await self.status_after_ack(request_id, deadline) if response else reply

    async def status(self) -> dict:
        return await self.request("status", response=True)

    async def nvs(self) -> list[dict]:
        request_id = self.new_id()
        deadline = time.monotonic() + self.stream_timeout
        await self.write(encode_command(request_id, "nvs"))
        await self.ack(request_id, True, deadline)
        begun = False
        entries = []
        seen = set()
        while True:
            message = await self.next_message(deadline)
            kind = message.get("type")
            if kind not in {"nvs.begin", "nvs.entry", "nvs.end"}:
                continue
            require(type(message.get("id")) is int and message["id"] == request_id,
                    "NVS stream request ID mismatch")
            if kind == "nvs.begin":
                require(not begun, "Duplicate NVS begin frame")
                begun = True
            elif kind == "nvs.entry":
                require(begun, "NVS entry arrived before begin")
                key = message.get("key")
                require(isinstance(key, str) and bool(key) and key not in seen,
                        "NVS key is missing or duplicated")
                require(type(message.get("stored")) is bool and "value" in message,
                        "NVS entry is missing value or stored flag")
                require(message["value"] is None or type(message["value"]) in (str, bool, int, float),
                        "NVS values must be JSON primitives")
                check_no_secrets(message)
                seen.add(key)
                entries.append(message)
                require(len(entries) <= 256, "NVS stream has too many entries")
            else:
                require(begun and REQUIRED_NVS <= seen, "NVS stream ended early or lacks required keys")
                return entries


class SerialTransport:
    def __init__(self, handle: Any):
        self.handle = handle

    @classmethod
    async def connect(cls, args: argparse.Namespace) -> SerialTransport:
        import serial
        # Set lines before open, avoiding the usual deliberate DTR/RTS reset.
        # Some USB drivers can still glitch these lines; see the hardware guide.
        handle = serial.Serial(port=None, baudrate=args.baud, timeout=0.1,
                               write_timeout=args.timeout, exclusive=True)
        handle.dtr = False
        handle.rts = False
        handle.port = args.port
        try:
            await asyncio.to_thread(handle.open)
            await asyncio.sleep(args.startup_wait)
        except BaseException:
            handle.close()
            raise
        return cls(handle)

    async def read(self, timeout: float) -> bytes:
        self.handle.timeout = min(timeout, 0.1)
        return await asyncio.to_thread(self.handle.read, min(max(self.handle.in_waiting, 1), 512))

    async def write(self, data: bytes) -> None:
        # No flush(): pyserial flush/tcdrain can wait forever on a stalled USB link.
        written = await asyncio.to_thread(self.handle.write, data)
        require(written == len(data), "Serial write was incomplete")

    async def close(self) -> None:
        await asyncio.to_thread(self.handle.close)


def stable_entries(entries: list[dict]) -> dict:
    return {entry["key"]: {"value": entry["value"], "stored": entry["stored"]}
            for entry in entries if entry["key"] in STABLE_NVS}


class SmokeSuite:
    def __init__(self, client: ProtocolClient, args: argparse.Namespace, report: dict):
        self.client = client
        self.args = args
        self.report = report
        self.snapshot: dict | None = None
        self.restore_needed = False
        self.absent_ssid: str | None = None

    async def check(self, label: str, operation: Any) -> Any:
        started = time.monotonic()
        record = {"name": label, "ok": False}
        self.report["tests"].append(record)
        try:
            result = await operation()
            record["ok"] = True
            print(f"PASS {label}")
            return result
        except SmokeFailure as exc:
            record["reason"] = str(exc)
            print(f"FAIL {label}: {exc}", file=sys.stderr)
            raise
        except BaseException:
            record["reason"] = "Transport, dependency, or interruption failure; raw details suppressed"
            raise
        finally:
            record["elapsed_sec"] = round(time.monotonic() - started, 3)

    async def unchanged(self) -> dict:
        status = await self.client.status()
        require(status["settings"] == self.snapshot["settings"], "Rejected request changed settings")
        require(status["networks"] == self.snapshot["networks"], "Rejected request changed saved networks")
        return status

    async def rejected_settings(self, fields: dict) -> None:
        # If buggy firmware partially applies the request, finally attempts rollback.
        self.restore_needed = True
        await self.client.request("settings", ok=False, **fields)
        await self.unchanged()

    async def rejected_wifi(self, command: str, **fields: Any) -> None:
        await self.client.request(command, ok=False, **fields)
        await self.unchanged()

    async def wait_wifi_idle(self) -> None:
        deadline = time.monotonic() + self.args.wifi_wait
        while True:
            status = await self.client.status()
            if status.get("wifi_state") not in {"connecting", "syncing"}:
                return
            require(time.monotonic() < deadline, "Boot Wi-Fi attempt did not finish within --wifi-wait")
            await asyncio.sleep(min(0.5, max(0, deadline - time.monotonic())))

    async def bad_frame(self, data: bytes) -> None:
        deadline = time.monotonic() + self.client.timeout
        await self.client.write(data)
        await self.client.ack(0, False, deadline)
        await self.unchanged()

    async def partial_frame(self) -> None:
        request_id = self.client.new_id()
        data = encode_command(request_id, "status")
        # Split inside JSON as well as before its final newline. Neither an
        # incomplete object nor a complete unterminated object may execute.
        midpoint = len(data) // 2
        for fragment in (data[:midpoint], data[midpoint:-1]):
            await self.client.write(fragment)
            quiet_until = time.monotonic() + 0.15
            while time.monotonic() < quiet_until:
                try:
                    message = await self.client.next_message(quiet_until)
                except SmokeFailure as exc:
                    if str(exc).startswith("Timed out"):
                        break
                    raise
                require(not (message.get("type") == "ack" and message.get("id") in (0, request_id)),
                        "Firmware handled an unterminated incoming frame")
        deadline = time.monotonic() + self.client.timeout
        await self.client.write(data[-1:])
        await self.client.ack(request_id, True, deadline)
        await self.client.status_after_ack(request_id, deadline)

    async def multiline(self) -> None:
        ids = [self.client.new_id(), self.client.new_id()]
        await self.client.write(b"".join(encode_command(i, "status") for i in ids))
        deadline = time.monotonic() + self.client.timeout * 2
        for request_id in ids:
            await self.client.ack(request_id, True, deadline)
            await self.client.status_after_ack(request_id, deadline)

    async def exercise(self) -> None:
        original = self.snapshot["settings"]
        # Only reversible settings. No time, scale, totals, peers, or Wi-Fi writes.
        self.restore_needed = True
        fields = {"dnd": not original["dnd"], "beep_type": (original["beep_type"] + 1) % 4,
                  "color": original["color"] ^ 0x010101}
        status = await self.client.request("settings", response=True, **fields)
        require(all(status["settings"][key] == value for key, value in fields.items()),
                "Exercise settings did not round-trip")

    async def restore(self) -> None:
        if self.snapshot is None or not self.restore_needed:
            return
        self.report["restoration"] = {"attempted": True, "ok": False, "wrote_settings": False,
                                      "removed_unexpected_test_network": False}
        status = await self.client.status()
        if self.absent_ssid and any(network["ssid"] == self.absent_ssid for network in status["networks"]):
            # Only remove a name proven absent in the original snapshot. A broken
            # password validator must not leave our test network behind.
            await self.wait_wifi_idle()
            await self.client.request("wifi.remove", response=True, ssid=self.absent_ssid)
            self.report["restoration"]["removed_unexpected_test_network"] = True
            status = await self.client.status()
        original = self.snapshot["settings"]
        if status["settings"] != original:
            # Normally just dnd/beep/color. Recover any unexpected accepted invalid
            # field too, using only the original valid settings, never stale NVS.
            fields = {key: original[key] for key in SETTING_KEYS if status["settings"][key] != original[key]}
            restored = await self.client.request("settings", response=True, **fields)
            self.report["restoration"]["wrote_settings"] = True
            require(restored["settings"] == original, "Settings restoration did not round-trip")
        self.report["restoration"]["ok"] = True
        self.report["final_status"] = sanitize(await self.unchanged())

    async def run(self) -> None:
        self.snapshot = await self.check("status.protocol2", self.client.status)
        self.report["initial_status"] = sanitize(self.snapshot)
        initial_nvs = await self.check("nvs.complete_and_redacted", self.client.nvs)
        self.report["initial_nvs"] = sanitize(initial_nvs)
        await self.check("wifi.boot_attempt_bounded", self.wait_wifi_idle)
        for label, fields in invalid_settings(self.snapshot["settings"]):
            await self.check(label, lambda fields=fields: self.rejected_settings(fields))
        saved = {network["ssid"] for network in self.snapshot["networks"]}
        absent = next(f"smoke-absent-{i}" for i in range(5) if f"smoke-absent-{i}" not in saved)
        self.absent_ssid = absent
        for label, password in (("short", "short!"), ("long", "x" * 64), ("type", 123)):
            await self.check(f"wifi.reject_password.{label}",
                             lambda password=password: self.rejected_wifi("wifi.add", ssid=absent, password=password))
        await self.check("wifi.remove_unknown_safe", lambda: self.rejected_wifi("wifi.remove", ssid=absent))
        await self.check("command.unknown", lambda: self.rejected_wifi("smoke.unknown"))
        await self.check("frame.malformed", lambda: self.bad_frame(b'{"id":100,"cmd":\n'))
        await self.check("frame.oversize", lambda: self.bad_frame(b'{"id":101,"cmd":"' + b"x" * (MAX_LINE + 1) + b'"}\n'))
        await self.check("frame.partial_incoming", self.partial_frame)
        await self.check("frame.multiline_incoming", self.multiline)
        final_nvs = await self.check("nvs.final_complete", self.client.nvs)
        self.report["final_nvs"] = sanitize(final_nvs)

        async def persisted_unchanged() -> None:
            require(stable_entries(initial_nvs) == stable_entries(final_nvs),
                    "Rejected requests changed persisted settings, credentials, or calibration")
        await self.check("nvs.no_persistent_mutation", persisted_unchanged)
        if self.args.exercise:
            await self.check("settings.reversible_exercise", self.exercise)
        self.report["final_status"] = sanitize(await self.check("status.responsive_afterward", self.client.status))


def common_arguments(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--output", type=Path, default=Path("hardware-smoke.json"),
                        help="Sanitized JSON summary (default: hardware-smoke.json)")
    parser.add_argument("--timeout", type=float, default=12, help="Command deadline in seconds (default: 12)")
    parser.add_argument("--stream-timeout", type=float, default=45, help="Whole NVS-stream deadline (default: 45)")
    parser.add_argument("--wifi-wait", type=float, default=90, help="Bounded wait for an existing boot Wi-Fi attempt")
    parser.add_argument("--exercise", action="store_true", help="Write reversible DND/beep/color settings; restore in finally")


def validate_arguments(parser: argparse.ArgumentParser, args: argparse.Namespace) -> None:
    for name in ("timeout", "stream_timeout", "wifi_wait"):
        value = getattr(args, name)
        if not math.isfinite(value) or value <= 0:
            parser.error(f"--{name.replace('_', '-')} must be finite and positive")
    if hasattr(args, "startup_wait") and (not math.isfinite(args.startup_wait) or args.startup_wait < 0):
        parser.error("--startup-wait must be finite and nonnegative")


async def run_hardware(args: argparse.Namespace, connector: Any, kind: str, target: str) -> int:
    report = {"schema": 1, "started_utc": datetime.now(timezone.utc).isoformat(),
              "transport": kind, "target": target, "expected_board": "classic ESP32-WROOM, 4 MB flash (not probed)",
              "exercise": args.exercise, "ok": False, "tests": [],
              "restoration": {"attempted": False, "ok": None},
              "limitations": ["No flash, reset, tare, calibration, clock changes, total resets, or synthetic drinks",
                              "Keep the bottle still; autonomous hydration/clock processing continues",
                              "Status has no request ID; exclusive transport ownership is required",
                              "Physical weight, acoustic, lighting, and reboot persistence checks are manual"]}
    if kind == "ble":
        report["limitations"].append("Explicitly authorized BLE connection may persist a new peer MAC; not restored")
    transport = None
    suite = None
    exit_code = 1
    try:
        print("Connecting only to the explicitly supplied target. Stop if a backup/flasher owns it.")
        transport = await connector(args)
        client = ProtocolClient(transport, args.timeout, args.stream_timeout)
        suite = SmokeSuite(client, args, report)
        await suite.run()
        report["ok"] = True
        exit_code = 0
    except SmokeFailure as exc:
        report["failure"] = str(exc)
    except asyncio.CancelledError:
        report["failure"] = "Interrupted; restoration was attempted if needed"
        exit_code = 130
    except Exception as exc:
        # Some libraries include full RX/TX buffers in exception text. Never print it.
        report["failure"] = "Connection, dependency, or transport failure; raw details suppressed"
        report["exception_type"] = type(exc).__name__
    finally:
        if suite is not None:
            try:
                await suite.check("settings.finally_restore", suite.restore)
            except BaseException:
                report["ok"] = False
                report["failure"] = "Cleanup failed; inspect and restore settings using the sanitized initial snapshot"
                exit_code = 1
            report["decoder_stats"] = suite.client.decoder.stats
        if transport is not None:
            try:
                await asyncio.wait_for(transport.close(), args.timeout)
            except BaseException:
                report["ok"] = False
                report["failure"] = "Transport did not close cleanly"
                exit_code = 1
        report["finished_utc"] = datetime.now(timezone.utc).isoformat()
        report["passed"] = sum(test["ok"] for test in report["tests"])
        report["failed"] = sum(not test["ok"] for test in report["tests"])
        try:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(json.dumps(sanitize(report), indent=2, allow_nan=False) + "\n", encoding="utf-8")
        except Exception:
            print("FAIL could not write the sanitized JSON artifact", file=sys.stderr)
            exit_code = 1
        print(f"{'PASS' if exit_code == 0 else 'FAIL'}: {report['passed']} passed, {report['failed']} failed; sanitized summary written if output was writable")
    return exit_code


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="Explicit serial device; do not share with a monitor/flasher")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--startup-wait", type=float, default=2, help="Seconds to allow any USB-open boot output to settle")
    common_arguments(parser)
    args = parser.parse_args(argv)
    validate_arguments(parser, args)
    if args.baud <= 0:
        parser.error("--baud must be positive")
    try:
        return asyncio.run(run_hardware(args, SerialTransport.connect, "serial", args.port))
    except KeyboardInterrupt:
        return 130


if __name__ == "__main__":
    sys.exit(main())
