"""Offline BLE adapter tests. No bleak import, Bluetooth scan, or connection."""
import asyncio
import unittest

from ble_smoke import BleTransport, COMMAND_UUID, EVENT_UUID
from serial_smoke import SmokeFailure, encode_command


class FakeGatt:
    def __init__(self):
        self.is_connected = True
        self.writes = []
        self.stopped = []
        self.disconnected = False
        self.fail_stop = False

    async def write_gatt_char(self, uuid, data, response):
        self.writes.append((uuid, data, response))

    async def stop_notify(self, uuid):
        self.stopped.append(uuid)
        if self.fail_stop:
            raise RuntimeError("Notification stop failed")

    async def disconnect(self):
        self.is_connected = False
        self.disconnected = True


class BleTransportTests(unittest.IsolatedAsyncioTestCase):
    async def test_writes_minimum_mtu_chunks_with_responses(self):
        client = FakeGatt()
        transport = BleTransport(client, timeout=0.1)
        command = encode_command(11, "status", padding="x" * 70)
        await transport.write(command)
        self.assertGreater(len(client.writes), 1)
        self.assertTrue(all(uuid == COMMAND_UUID and response is True and len(data) <= 20
                            for uuid, data, response in client.writes))
        self.assertEqual(b"".join(data for _, data, _ in client.writes), command)

    async def test_notifications_queued_without_assuming_line_boundaries(self):
        transport = BleTransport(FakeGatt(), timeout=0.1)
        for part in (b'{"type":', b'"ack"}\n{"id":2}\n'):
            transport.notification(None, bytearray(part))
        self.assertEqual(await transport.read(0.01), b'{"type":')
        self.assertEqual(await transport.read(0.01), b'"ack"}\n{"id":2}\n')
        self.assertEqual(await transport.read(0.001), b"")

    async def test_pending_notifications_are_bounded(self):
        transport = BleTransport(FakeGatt(), timeout=0.1)
        for _ in range(513):
            transport.notification(None, bytearray(b"x" * 20))
        self.assertEqual(transport.incoming.qsize(), 512)
        with self.assertRaisesRegex(SmokeFailure, "overflowed"):
            await transport.read(0.01)

    async def test_disconnected_client_fails_without_waiting(self):
        client = FakeGatt()
        client.is_connected = False
        transport = BleTransport(client, timeout=0.1)
        with self.assertRaisesRegex(SmokeFailure, "disconnected"):
            await transport.read(0.01)
        with self.assertRaisesRegex(SmokeFailure, "disconnected"):
            await transport.write(b"{}\n")

    async def test_close_unsubscribes_and_disconnects(self):
        client = FakeGatt()
        transport = BleTransport(client, timeout=0.1)
        transport.subscribed = True
        await transport.close()
        self.assertEqual(client.stopped, [EVENT_UUID])
        self.assertTrue(client.disconnected)

    async def test_disconnect_is_attempted_even_when_unsubscribe_fails(self):
        client = FakeGatt()
        client.fail_stop = True
        transport = BleTransport(client, timeout=0.1)
        transport.subscribed = True
        with self.assertRaises(RuntimeError):
            await transport.close()
        self.assertTrue(client.disconnected)


if __name__ == "__main__":
    unittest.main()
