import 'dart:async';

import 'package:flutter/foundation.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'package:shared_preferences/shared_preferences.dart';

import 'bottle_protocol.dart';

class ScannedBottle {
  const ScannedBottle({required this.id, required this.name, this.rssi = 0});
  final String id;
  final String name;
  final int rssi;
}

/// The controller never depends on platform channels. Tests inject this seam.
abstract interface class BottleTransport {
  Stream<List<ScannedBottle>> get discoveries;
  Stream<List<int>> get notifications;
  Stream<void> get disconnections;
  int get mtu;
  Future<void> scan();
  Future<void> stopScan();

  /// Must enable notifications before returning, with listeners already attached.
  Future<void> connect(String id);
  Future<void> write(List<int> bytes);
  Future<void> disconnect();
  Future<void> dispose();
}

abstract interface class SessionStore {
  Future<ScannedBottle?> load();
  Future<void> save(ScannedBottle? bottle);
}

class PreferencesSessionStore implements SessionStore {
  @override
  Future<ScannedBottle?> load() async {
    final prefs = await SharedPreferences.getInstance();
    final id = prefs.getString('bottle.last.id');
    return id == null
        ? null
        : ScannedBottle(
            id: id,
            name: prefs.getString('bottle.last.name') ?? 'My bottle',
          );
  }

  @override
  Future<void> save(ScannedBottle? bottle) async {
    final prefs = await SharedPreferences.getInstance();
    if (bottle == null) {
      await prefs.remove('bottle.last.id');
      await prefs.remove('bottle.last.name');
    } else {
      await prefs.setString('bottle.last.id', bottle.id);
      await prefs.setString('bottle.last.name', bottle.name);
    }
  }
}

class FlutterBottleTransport implements BottleTransport {
  final _discoveries = StreamController<List<ScannedBottle>>.broadcast();
  final _notifications = StreamController<List<int>>.broadcast();
  final _disconnections = StreamController<void>.broadcast();
  StreamSubscription<List<ScanResult>>? _scanSub;
  StreamSubscription<List<int>>? _notifySub;
  StreamSubscription<BluetoothConnectionState>? _connectionSub;
  BluetoothDevice? _device;
  BluetoothCharacteristic? _command;
  bool _closing = false;
  int _generation = 0;
  int _scanGeneration = 0;
  Future<void>? _privateLogging;

  // The plugin defaults to debug logging, including GATT byte payloads.
  // Disable native and Dart packet logs before any credential can be written.
  Future<void> _prepare() => _privateLogging ??= FlutterBluePlus.setLogLevel(
    LogLevel.none,
    color: false,
  );

  @override
  Stream<List<ScannedBottle>> get discoveries => _discoveries.stream;
  @override
  Stream<List<int>> get notifications => _notifications.stream;
  @override
  Stream<void> get disconnections => _disconnections.stream;
  @override
  int get mtu => _device?.mtuNow ?? 23;

  @override
  Future<void> scan() async {
    final generation = ++_scanGeneration;
    await _prepare();
    if (!await FlutterBluePlus.isSupported) {
      throw StateError('Bluetooth unavailable');
    }
    await FlutterBluePlus.adapterState
        .where((s) => s == BluetoothAdapterState.on)
        .first
        .timeout(const Duration(seconds: 8));
    if (generation != _scanGeneration) return;
    await _scanSub?.cancel();
    final subscription = FlutterBluePlus.scanResults.listen((results) {
      if (generation != _scanGeneration) return;
      _discoveries.add(
        results
            .where(
              (r) =>
                  r.advertisementData.serviceUuids.any(
                    (u) => u.str.toLowerCase() == BottleProtocol.serviceUuid,
                  ) ||
                  r.advertisementData.advName.toLowerCase().contains(
                    'bottle',
                  ) ||
                  r.device.platformName.toLowerCase().contains('bottle'),
            )
            .map(
              (r) => ScannedBottle(
                id: r.device.remoteId.str,
                name: r.advertisementData.advName.isNotEmpty
                    ? r.advertisementData.advName
                    : (r.device.platformName.isEmpty
                          ? 'Water bottle'
                          : r.device.platformName),
                rssi: r.rssi,
              ),
            )
            .toList(),
      );
    }, onError: (_) => _discoveries.addError(StateError('Scan failed')));
    _scanSub = subscription;
    try {
      await FlutterBluePlus.startScan(timeout: const Duration(seconds: 10));
      await FlutterBluePlus.isScanning
          .where((value) => !value)
          .first
          .timeout(const Duration(seconds: 12));
    } finally {
      await subscription.cancel();
      if (identical(_scanSub, subscription)) _scanSub = null;
    }
  }

  @override
  Future<void> stopScan() async {
    ++_scanGeneration;
    await FlutterBluePlus.stopScan();
  }

  @override
  Future<void> connect(String id) async {
    final generation = ++_generation;
    _closing = false;
    final device = BluetoothDevice.fromId(id);
    _device = device;
    void checkCurrent() {
      if (generation != _generation || _closing) {
        throw StateError('Connection canceled');
      }
    }

    try {
      await _prepare();
      checkCurrent();
      // Disable implicit Android MTU request. Apple negotiates automatically;
      // Android's optional request is best effort. Frames stay <=20 regardless.
      await device.connect(
        license: License.nonprofit,
        timeout: const Duration(seconds: 12),
        mtu: null,
      );
      checkCurrent();
      _connectionSub = device.connectionState.listen((state) {
        if (state == BluetoothConnectionState.disconnected && !_closing) {
          _command = null;
          _disconnections.add(null);
        }
      });
      if (!kIsWeb && defaultTargetPlatform == TargetPlatform.android) {
        try {
          await device.requestMtu(185, timeout: 4);
        } catch (_) {
          /* 23-byte ATT is sufficient. */
        }
      }
      checkCurrent();
      final services = await device.discoverServices(timeout: 10);
      checkCurrent();
      BluetoothCharacteristic? event;
      for (final service in services) {
        if (service.uuid.str.toLowerCase() != BottleProtocol.serviceUuid) {
          continue;
        }
        for (final characteristic in service.characteristics) {
          final uuid = characteristic.uuid.str.toLowerCase();
          if (uuid == BottleProtocol.cmdUuid) _command = characteristic;
          if (uuid == BottleProtocol.evtUuid) event = characteristic;
        }
      }
      if (_command == null ||
          event == null ||
          !event.properties.notify ||
          !(_command!.properties.write ||
              _command!.properties.writeWithoutResponse)) {
        throw StateError('Protocol characteristics unavailable');
      }
      // Listener first: enabling CCCD or writing can synchronously deliver data.
      _notifySub = event.onValueReceived.listen(
        _notifications.add,
        onError: (_) =>
            _notifications.addError(StateError('Notification failed')),
      );
      await event.setNotifyValue(true, timeout: 8);
      checkCurrent();
    } catch (_) {
      // A canceled old attempt must not tear down a newer connection.
      if (generation == _generation) await disconnect();
      rethrow;
    }
  }

  @override
  Future<void> write(List<int> bytes) async {
    final command = _command;
    if (command == null) throw StateError('Not connected');
    await command.write(
      bytes,
      withoutResponse: !command.properties.write,
      timeout: 5,
    );
  }

  @override
  Future<void> disconnect() async {
    _closing = true;
    ++_generation;
    _command = null;
    await _notifySub?.cancel();
    await _connectionSub?.cancel();
    _notifySub = null;
    _connectionSub = null;
    final device = _device;
    _device = null;
    if (device != null) {
      try {
        await device.disconnect(timeout: 6, queue: false);
      } catch (_) {
        /* Already gone. */
      }
    }
  }

  @override
  Future<void> dispose() async {
    try {
      await stopScan();
    } catch (_) {}
    await _scanSub?.cancel();
    await disconnect();
    await _discoveries.close();
    await _notifications.close();
    await _disconnections.close();
  }
}
