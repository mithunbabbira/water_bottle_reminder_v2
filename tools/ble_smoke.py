#!/usr/bin/env python3
"""Optional BLE protocol-v2 smoke suite; use a separate venv with bleak.

No scan or connection occurs on import or --help. Supply an explicit address
(macOS: CoreBluetooth UUID). Connection itself can update the firmware's saved
peer MAC; --allow-peer-update acknowledges that non-reversible side effect.
"""
from __future__ import annotations

import argparse
import asyncio
import sys

from serial_smoke import (SmokeFailure, chunks, common_arguments, require,
                          run_hardware, validate_arguments)

SERVICE_UUID = "6b6f7474-6c65-4001-b5a3-000000000001"
COMMAND_UUID = "6b6f7474-6c65-4002-b5a3-000000000001"
EVENT_UUID = "6b6f7474-6c65-4003-b5a3-000000000001"


class BleTransport:
    def __init__(self, client, timeout: float):
        self.client = client
        self.timeout = timeout
        self.incoming: asyncio.Queue[bytes] = asyncio.Queue(maxsize=512)
        self.overflowed = False
        self.subscribed = False

    def notification(self, _sender, data: bytearray) -> None:
        # Never print notifications: even broken firmware must not leak secrets.
        try:
            self.incoming.put_nowait(bytes(data))
        except asyncio.QueueFull:
            self.overflowed = True

    @classmethod
    async def connect(cls, args: argparse.Namespace) -> BleTransport:
        from bleak import BleakClient
        print("WARNING BLE connection may overwrite the saved prototype peer MAC; it cannot be restored by this suite.")
        client = BleakClient(args.address, timeout=args.timeout, services=[SERVICE_UUID])
        transport = cls(client, args.timeout)
        try:
            await asyncio.wait_for(client.connect(), args.timeout)
            require(client.is_connected, "BLE did not connect")
            command = client.services.get_characteristic(COMMAND_UUID)
            events = client.services.get_characteristic(EVENT_UUID)
            require(command is not None and events is not None, "Bottle GATT characteristics were not found")
            require("write" in command.properties and "notify" in events.properties,
                    "Bottle GATT write-with-response/notify properties are missing")
            # Subscribe BEFORE status or NVS, including the ACK notifications.
            await asyncio.wait_for(client.start_notify(EVENT_UUID, transport.notification), args.timeout)
            transport.subscribed = True
            return transport
        except BaseException:
            try:
                await asyncio.wait_for(client.disconnect(), args.timeout)
            except BaseException:
                pass
            raise

    async def read(self, timeout: float) -> bytes:
        require(not self.overflowed, "BLE notification pending queue overflowed")
        require(self.client.is_connected, "BLE disconnected while waiting for a response")
        try:
            return await asyncio.wait_for(self.incoming.get(), timeout)
        except asyncio.TimeoutError:
            return b""

    async def write(self, data: bytes) -> None:
        require(self.client.is_connected, "BLE disconnected before a write")
        # MTU 23 -> 20-byte ATT payload, even when the OS negotiates a larger MTU.
        # These are fragments of one newline-terminated command, not independent
        # commands. Await every GATT write response for bounded backpressure.
        for fragment in chunks(data, 20):
            await asyncio.wait_for(self.client.write_gatt_char(COMMAND_UUID, fragment, response=True), self.timeout)

    async def close(self) -> None:
        try:
            if self.subscribed and self.client.is_connected:
                await asyncio.wait_for(self.client.stop_notify(EVENT_UUID), self.timeout / 2)
        finally:
            await asyncio.wait_for(self.client.disconnect(), self.timeout / 2)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--address", required=True, help="Explicit BLE address or macOS CoreBluetooth peripheral UUID")
    parser.add_argument("--allow-peer-update", action="store_true",
                        help="Required: acknowledge connecting may overwrite saved peer MAC (not reversible)")
    common_arguments(parser)
    args = parser.parse_args(argv)
    validate_arguments(parser, args)
    if not args.allow_peer_update:
        parser.error("BLE connection can persist a different peer MAC; use serial for no peer changes, or explicitly pass --allow-peer-update")
    try:
        return asyncio.run(run_hardware(args, BleTransport.connect, "ble", args.address))
    except KeyboardInterrupt:
        return 130


if __name__ == "__main__":
    sys.exit(main())
