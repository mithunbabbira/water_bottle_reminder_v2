import 'dart:async';
import 'dart:convert';

import 'package:bottle_tester/bottle_protocol.dart';
import 'package:bottle_tester/bottle_transport.dart';

const testBottle = ScannedBottle(
  id: 'explicit-device-id',
  name: 'Kitchen bottle',
  rssi: -42,
);

Map<String, dynamic> deviceStatus() => {
  'type': 'status',
  'protocol': 2,
  'firmware': '2.0.0',
  'ready': true,
  'clock_valid': true,
  'time_source': 'ntp',
  'epoch': 1790000000,
  'tz_min': 330,
  'active': true,
  'weight_g': 650.5,
  'raw': 123,
  'scale_state': 'ready',
  'state': 'idle',
  'baseline_g': 700,
  'today_ml': 250,
  'last_drink_ml': 50,
  'next_check_sec': 900,
  'phone_connected': true,
  'phone_present': true,
  'phone_mac': 'AA:BB:CC:DD:EE:FF',
  'wifi_state': 'idle',
  'wifi_error': '',
  'wifi_ssid': '',
  'settings': <String, dynamic>{
    'start_min': 420,
    'end_min': 1260,
    'interval_min': 45,
    'beep_type': 1,
    'dnd': false,
    'color': 0x8000ff,
    'reminder_color': 0x00c8ff,
    'missing_color': 0xffb400,
    'missing_sec': 120,
    'beep_delay_sec': 3,
    'drink_grams': 15,
    'settle_ms': 1000,
    'presence_enabled': false,
    'tz_min': 330,
  },
  'networks': <Map<String, dynamic>>[],
};

class MemorySessionStore implements SessionStore {
  ScannedBottle? saved;
  int writes = 0;
  @override
  Future<ScannedBottle?> load() async => saved;
  @override
  Future<void> save(ScannedBottle? bottle) async {
    saved = bottle;
    writes++;
  }
}

class FakeTransport implements BottleTransport {
  FakeTransport() {
    decoder = JsonLineDecoder(
      onMessage: (command) {
        commands.add(command);
        if (onCommand != null) {
          onCommand!(command);
        } else if (autoRespond) {
          respond(command);
        }
      },
      onError: (error) => throw StateError(error),
    );
  }
  final discoveryController = StreamController<List<ScannedBottle>>.broadcast(
    sync: true,
  );
  final notificationController = StreamController<List<int>>.broadcast(
    sync: true,
  );
  final disconnectController = StreamController<void>.broadcast(sync: true);
  final List<Map<String, dynamic>> commands = [];
  final List<List<int>> chunks = [];
  Map<String, dynamic> status = deviceStatus();
  List<Map<String, dynamic>> scanNetworks = [
    {'ssid': ' Living room Café 水 ', 'rssi': -43, 'secured': true},
    {'ssid': 'Guest', 'rssi': -79, 'secured': false},
  ];
  late JsonLineDecoder decoder;
  void Function(Map<String, dynamic>)? onCommand;
  Completer<void>? connectGate;
  bool autoRespond = true;
  bool failConnect = false;
  bool failWrite = false;
  bool active = false;
  bool disposed = false;
  bool listenerPresentOnConnect = false;
  int connects = 0;
  int disconnects = 0;
  int scans = 0;
  final List<String> connectedIds = [];
  @override
  int mtu = 23;
  @override
  Stream<List<ScannedBottle>> get discoveries => discoveryController.stream;
  @override
  Stream<List<int>> get notifications => notificationController.stream;
  @override
  Stream<void> get disconnections => disconnectController.stream;
  @override
  Future<void> scan() async {
    scans++;
    discoveryController.add([testBottle]);
  }

  @override
  Future<void> stopScan() async {}
  @override
  Future<void> connect(String id) async {
    connects++;
    connectedIds.add(id);
    listenerPresentOnConnect = notificationController.hasListener;
    active = true;
    if (connectGate != null) await connectGate!.future;
    if (failConnect) throw StateError('connect failure');
  }

  @override
  Future<void> write(List<int> bytes) async {
    if (failWrite) {
      throw StateError('write failure with password should not be logged');
    }
    if (!active) throw StateError('not connected');
    chunks.add(List.of(bytes));
    decoder.add(bytes);
  }

  void emit(Map<String, dynamic> message, {int chunkSize = 20}) {
    final bytes = utf8.encode('${jsonEncode(message)}\n');
    for (var i = 0; i < bytes.length; i += chunkSize) {
      notificationController.add(
        bytes.sublist(
          i,
          i + chunkSize > bytes.length ? bytes.length : i + chunkSize,
        ),
      );
    }
  }

  void respond(Map<String, dynamic> command, {bool ok = true, String? error}) {
    final cmd = command['cmd'];
    final id = command['id'];
    emit({'type': 'ack', 'id': id, 'ok': ok, 'error': ?error});
    if (!ok) return;
    if (cmd == 'settings') {
      (status['settings'] as Map<String, dynamic>).addAll(
        Map<String, dynamic>.of(command)
          ..remove('id')
          ..remove('cmd'),
      );
    } else if (cmd == 'wifi.add') {
      final networks = (status['networks'] as List)
          .map((n) => Map<String, dynamic>.from(n as Map))
          .toList();
      status['networks'] = networks;
      networks.removeWhere((n) => n['ssid'] == command['ssid']);
      networks.add({
        'ssid': command['ssid'],
        'secured': (command['password'] as String).isNotEmpty,
      });
      status['wifi_state'] = 'connecting';
    } else if (cmd == 'wifi.remove') {
      (status['networks'] as List).removeWhere(
        (n) => n['ssid'] == command['ssid'],
      );
    } else if (cmd == 'time.set') {
      status['time_source'] = 'app';
      status['tz_min'] = command['tz_min'];
    } else if (cmd == 'phone.clear') {
      status['phone_mac'] = null;
    }
    if (cmd == 'wifi.scan') {
      emit({
        'type': 'wifi.scan',
        'id': id,
        'ok': true,
        'networks': scanNetworks,
        'truncated': false,
        'hidden_count': 0,
      });
    } else if (cmd == 'nvs') {
      emit({'type': 'nvs.begin', 'id': id});
      emit({
        'type': 'nvs.entry',
        'id': id,
        'key': 'start_min',
        'value': 600,
        'stored': false,
      });
      emit({
        'type': 'nvs.entry',
        'id': id,
        'key': 'tare_offset',
        'value': 12,
        'stored': true,
      });
      emit({
        'type': 'nvs.entry',
        'id': id,
        'key': 'cal_factor',
        'value': 2.5,
        'stored': true,
      });
      emit({
        'type': 'nvs.entry',
        'id': id,
        'key': 'wifi_p0',
        'value': 'set',
        'stored': true,
      });
      emit({'type': 'nvs.end', 'id': id});
    } else {
      emit(status);
    }
  }

  void loseConnection() {
    active = false;
    disconnectController.add(null);
  }

  @override
  Future<void> disconnect() async {
    disconnects++;
    active = false;
    decoder.reset();
  }

  @override
  Future<void> dispose() async {
    disposed = true;
    await disconnect();
    await discoveryController.close();
    await notificationController.close();
    await disconnectController.close();
  }
}
