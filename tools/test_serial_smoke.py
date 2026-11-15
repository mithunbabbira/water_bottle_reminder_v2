"""Offline tests only: no serial, BLE, firmware, or application is started.

Run: python3 -m unittest discover -s tools -p 'test_*.py' -v
"""
import argparse
import asyncio
from contextlib import redirect_stderr, redirect_stdout
from copy import deepcopy
import io
import json
from pathlib import Path
import tempfile
import time
import unittest

from serial_smoke import (
    JsonLineDecoder, MAX_LINE, ProtocolClient, REQUIRED_NVS, SETTING_KEYS,
    SETTING_RANGES, SmokeFailure, check_no_secrets, chunks, encode_command,
    invalid_settings, run_hardware, sanitize, validate_status,
)


def status_message():
    settings = {key: low for key, (low, _high) in SETTING_RANGES.items()}
    settings.update(dnd=False, presence_enabled=False, tz_min=330, color=0x8000FF,
                    beep_type=2, interval_min=30, end_min=1380, start_min=600)
    return {"type": "status", "protocol": 2, "ready": True, "settings": settings,
            "networks": [], "wifi_state": "idle", "scale_state": "need_tare",
            "weight_g": None, "phone_mac": None}


def frame(message):
    return (json.dumps(message) + "\n").encode()


class MemoryTransport:
    def __init__(self, incoming=b""):
        self.incoming = bytearray(incoming)
        self.writes = []

    async def read(self, timeout):
        if not self.incoming:
            await asyncio.sleep(min(timeout, 0.001))
            return b""
        result = bytes(self.incoming[:37])  # Arbitrary partial transport reads.
        del self.incoming[:37]
        return result

    async def write(self, data):
        self.writes.append(data)

    async def close(self):
        pass


class FakeBottle(MemoryTransport):
    """Small protocol contract fake to test runner safety, not a firmware substitute."""

    def __init__(self, *, fail_exercise=False, accept_null=False):
        super().__init__()
        self.status = status_message()
        self.original = deepcopy(self.status)
        self.rx = bytearray()
        self.commands = []
        self.settings_writes = []
        self.fail_exercise = fail_exercise
        self.accept_null = accept_null
        self.closed = False

    def emit(self, message):
        self.incoming.extend(frame(message))

    async def write(self, data):
        self.writes.append(data)
        self.rx.extend(data)
        while b"\n" in self.rx:
            raw, _, remainder = self.rx.partition(b"\n")
            self.rx = bytearray(remainder)
            if len(raw) > MAX_LINE:
                self.emit({"type": "ack", "id": 0, "ok": False, "error": "Frame too long"})
                continue
            try:
                command = json.loads(raw)
            except ValueError:
                self.emit({"type": "ack", "id": 0, "ok": False, "error": "Invalid JSON"})
                continue
            self.commands.append(command)
            request_id = command["id"]
            kind = command["cmd"]
            ok = kind in {"status", "nvs", "settings"}
            if kind == "settings":
                fields = {key: val for key, val in command.items() if key in SETTING_KEYS}
                updated = {**self.status["settings"], **fields}
                if self.accept_null:
                    updated = {key: self.status["settings"][key] if val is None else val
                               for key, val in updated.items()}
                try:
                    validate_status({**self.status, "settings": updated})
                except SmokeFailure:
                    ok = False
                if ok:
                    self.status["settings"] = updated
                    self.settings_writes.append(fields)
            self.emit({"type": "ack", "id": request_id, "ok": ok, **({} if ok else {"error": "Rejected"})})
            if kind == "nvs" and ok:
                self.emit({"type": "nvs.begin", "id": request_id})
                for key in sorted(REQUIRED_NVS):
                    value = self.status["settings"].get(key, None)
                    if key == "ble_irk" or key == "ap_pass" or key.startswith("wifi_p"):
                        value = "empty"
                    self.emit({"type": "nvs.entry", "id": request_id, "key": key,
                               "value": value, "stored": False})
                self.emit({"type": "nvs.end", "id": request_id})
            elif ok:
                snapshot = deepcopy(self.status)
                if kind == "settings" and self.fail_exercise:
                    snapshot["settings"]["dnd"] = not snapshot["settings"]["dnd"]
                    self.fail_exercise = False
                self.emit(snapshot)

    async def close(self):
        self.closed = True


class FramingTests(unittest.TestCase):
    def test_boot_logs_partial_utf8_and_crlf(self):
        decoder = JsonLineDecoder()
        expected = {"type": "status", "ssid": "café 水"}
        wire = b"ets Jun  8 2016 00:22:57\r\nrst:0x1\r\n" + frame(expected).replace(b"\n", b"\r\n")
        received = []
        for byte in wire:
            received.extend(decoder.feed(bytes([byte])))
        self.assertEqual(received, [expected])
        self.assertEqual(decoder.stats["ignored_lines"], 2)

    def test_many_lines_and_partial_tail(self):
        decoder = JsonLineDecoder()
        self.assertEqual(decoder.feed(b'\n{"a":1}\n{"b":2}\n{"c":'), [{"a": 1}, {"b": 2}])
        self.assertEqual(decoder.feed(b'3}\n'), [{"c": 3}])

    def test_unterminated_json_is_not_a_frame(self):
        decoder = JsonLineDecoder()
        self.assertEqual(decoder.feed(b'{"a":1}'), [])
        self.assertEqual(decoder.feed(b'\n'), [{"a": 1}])

    def test_overlong_line_discarded_until_newline(self):
        decoder = JsonLineDecoder(max_line=12)
        self.assertEqual(decoder.feed(b"x" * 100_000), [])
        self.assertEqual(len(decoder.buffer), 0)
        self.assertTrue(decoder.discarding)
        self.assertEqual(decoder.feed(b'{"bad":1}\n{"ok":1}\n'), [{"ok": 1}])
        self.assertEqual(decoder.stats["oversize_lines"], 1)

    def test_exact_line_limit_is_valid(self):
        line = b'{"ok":1}'
        decoder = JsonLineDecoder(max_line=len(line))
        self.assertEqual(decoder.feed(line + b"\n"), [{"ok": 1}])

    def test_bad_json_utf8_and_nonfinite_are_ignored_without_echo(self):
        decoder = JsonLineDecoder()
        data = b'{not json}\n{"bad":"\xff"}\n{"x":NaN}\n[1,2]\n{"ok":true}\n'
        self.assertEqual(decoder.feed(data), [{"ok": True}])
        self.assertEqual(decoder.stats["invalid_json"], 3)
        self.assertNotIn("not json", repr(decoder.stats))

    def test_minimum_mtu_chunks_preserve_newline_and_utf8_bytes(self):
        command = encode_command(42, "wifi.add", ssid="水" * 10, password="not-printed")
        fragmented = chunks(command)
        self.assertTrue(all(0 < len(chunk) <= 20 for chunk in fragmented))
        self.assertEqual(b"".join(fragmented), command)
        self.assertTrue(command.endswith(b"\n"))
        self.assertEqual(json.loads(command)["id"], 42)
        with self.assertRaises(ValueError):
            chunks(command, 0)

    def test_secret_sanitization_covers_nested_nvs_envelopes(self):
        sentinel = "NEVER-LOG-THIS-SECRET"
        raw = {"password": sentinel, "nested": [{"key": "wifi_p0", "value": sentinel},
               {"key": "ble_irk", "value": [1, 2, 3]}, {"key": "ap_pass", "value": "empty"}],
               "sta_pass": {"unexpected": sentinel}, "phone_mac": "ab:cd",
               "wifi_error": sentinel, "beep_type": 2}
        safe = sanitize(raw)
        self.assertNotIn(sentinel, json.dumps(safe))
        self.assertNotIn("ab:cd", json.dumps(safe))
        self.assertEqual(safe["nested"][2]["value"], "empty")
        self.assertEqual(safe["beep_type"], 2)

    def test_firmware_secret_leaks_fail_without_echoing_value(self):
        with self.assertRaises(SmokeFailure) as raised:
            check_no_secrets({"type": "nvs.entry", "key": "ble_irk", "value": "PRIVATE"})
        self.assertNotIn("PRIVATE", str(raised.exception))
        check_no_secrets({"key": "wifi_p3", "value": "set"})

    def test_invalid_settings_cases_have_valid_changed_companion(self):
        original = status_message()["settings"]
        cases = invalid_settings(original)
        self.assertGreaterEqual(len(cases), 50)
        self.assertEqual(len({name for name, _ in cases}), len(cases))
        for _name, fields in cases:
            self.assertTrue(fields.get("dnd") is not original["dnd"]
                            or fields.get("color") == original["color"] ^ 0x010101)

    def test_status_validation_allows_uncalibrated_not_unready_storage(self):
        validate_status(status_message())
        with self.assertRaises(SmokeFailure):
            validate_status({**status_message(), "ready": False})
        changed = status_message()
        changed["settings"]["interval_min"] = True
        with self.assertRaises(SmokeFailure):
            validate_status(changed)


class ClientTests(unittest.IsolatedAsyncioTestCase):
    async def test_status_waits_for_matching_ack_then_response(self):
        old = {**status_message(), "marker": "old"}
        new = {**status_message(), "marker": "new"}
        transport = MemoryTransport(frame(old) + frame({"type": "ack", "id": 99, "ok": True})
                                    + frame({"type": "ack", "id": 1001, "ok": True}) + frame(new))
        result = await ProtocolClient(transport, timeout=0.1).status()
        self.assertEqual(result["marker"], "new")
        self.assertEqual(json.loads(transport.writes[0])["id"], 1001)

    async def test_deadline_without_complete_frame(self):
        client = ProtocolClient(MemoryTransport(b'{"type":"ack"'), timeout=0.015)
        started = time.monotonic()
        with self.assertRaisesRegex(SmokeFailure, "Timed out"):
            await client.status()
        self.assertLess(time.monotonic() - started, 0.5)

    async def test_wrong_ack_result_is_not_success(self):
        wire = frame({"type": "ack", "id": 1001, "ok": False, "error": "PRIVATE"})
        with self.assertRaisesRegex(SmokeFailure, "ACK result") as raised:
            await ProtocolClient(MemoryTransport(wire), timeout=0.1).status()
        self.assertNotIn("PRIVATE", str(raised.exception))

    async def test_nvs_requires_begin_and_end_with_matching_id(self):
        prefix = frame({"type": "ack", "id": 1001, "ok": True})
        bad_sequences = [
            [{"type": "nvs.entry", "id": 1001, "key": "x", "value": 1, "stored": False}],
            [{"type": "nvs.begin", "id": 1002}],
            [{"type": "nvs.begin", "id": 1001}, {"type": "nvs.end", "id": 1001}],
            [{"type": "nvs.begin", "id": 1001}, {"type": "nvs.begin", "id": 1001}],
        ]
        for sequence in bad_sequences:
            with self.subTest(sequence=sequence), self.assertRaises(SmokeFailure):
                client = ProtocolClient(MemoryTransport(prefix + b"".join(frame(x) for x in sequence)),
                                        timeout=0.1, stream_timeout=0.1)
                await client.nvs()

    async def test_full_nvs_is_not_cut_off_after_ack_or_first_entry(self):
        bottle = FakeBottle()
        entries = await ProtocolClient(bottle, timeout=0.1, stream_timeout=0.1).nvs()
        self.assertEqual({entry["key"] for entry in entries}, REQUIRED_NVS)
        self.assertTrue(all(entry["stored"] is False for entry in entries))

    async def test_duplicate_nvs_keys_rejected(self):
        entry = {"type": "nvs.entry", "id": 1001, "key": "schema", "value": 2, "stored": False}
        wire = b"".join(frame(item) for item in [
            {"type": "ack", "id": 1001, "ok": True}, {"type": "nvs.begin", "id": 1001}, entry, entry])
        with self.assertRaisesRegex(SmokeFailure, "duplicated"):
            await ProtocolClient(MemoryTransport(wire), stream_timeout=0.1).nvs()

    async def run_fake(self, bottle, exercise=False):
        with tempfile.TemporaryDirectory(prefix=".smoke-test-", dir=Path(__file__).parent) as directory:
            output = Path(directory) / "summary.json"
            args = argparse.Namespace(timeout=0.2, stream_timeout=0.2, wifi_wait=0.2,
                                      exercise=exercise, output=output)

            async def connector(_args):
                return bottle

            with redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
                code = await run_hardware(args, connector, "offline-fake", "not-hardware")
            return code, json.loads(output.read_text())

    async def test_default_suite_never_intentionally_writes_and_finishes(self):
        bottle = FakeBottle()
        code, report = await self.run_fake(bottle)
        self.assertEqual(code, 0, report)
        self.assertTrue(report["ok"])
        self.assertEqual(bottle.settings_writes, [])
        self.assertTrue(bottle.closed)
        self.assertFalse(report["restoration"]["wrote_settings"])
        forbidden = {"tare", "calibrate", "time.set", "phone.clear", "wifi.sync"}
        self.assertFalse(forbidden & {command["cmd"] for command in bottle.commands})

    async def test_exercise_restores_original_settings_in_finally(self):
        bottle = FakeBottle()
        code, report = await self.run_fake(bottle, exercise=True)
        self.assertEqual(code, 0, report)
        self.assertEqual(bottle.status, bottle.original)
        self.assertEqual(len(bottle.settings_writes), 2)
        self.assertTrue(report["restoration"]["ok"])
        self.assertTrue(report["restoration"]["wrote_settings"])

    async def test_failed_exercise_still_restores_and_fails_exit(self):
        bottle = FakeBottle(fail_exercise=True)
        code, report = await self.run_fake(bottle, exercise=True)
        self.assertEqual(code, 1)
        self.assertFalse(report["ok"])
        self.assertTrue(report["restoration"]["ok"])
        self.assertEqual(bottle.status, bottle.original)

    async def test_buggy_null_acceptance_fails_and_rolls_back(self):
        bottle = FakeBottle(accept_null=True)
        code, report = await self.run_fake(bottle)
        self.assertEqual(code, 1)
        self.assertFalse(report["ok"])
        self.assertTrue(report["restoration"]["ok"])
        self.assertEqual(bottle.status, bottle.original)
        self.assertEqual(report["failed"], 1)


if __name__ == "__main__":
    unittest.main()
