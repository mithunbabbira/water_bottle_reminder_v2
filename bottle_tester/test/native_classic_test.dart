import 'package:bottle_tester/bottle_ble_controller.dart';
import 'package:bottle_tester/classic_discovery.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';

import 'fakes.dart';

class _Discovery implements ClassicDiscovery {
  bool stale = false;
  int scans = 0, cancels = 0;
  @override
  Future<Map<String, dynamic>> scan(int requestId) async {
    scans++;
    return {
      'id': stale ? requestId + 1 : requestId,
      'ok': true,
      'discovery_source': 'mac_classic',
      'truncated': false,
      'devices': [
        {'address': 'AA:BB:CC:DD:EE:FF', 'name': 'Galaxy 水', 'rssi': -40},
      ],
    };
  }

  @override
  Future<void> cancel() async {
    cancels++;
  }
}

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  const channel = MethodChannel('test/classic_discovery');
  tearDown(
    () => TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
        .setMockMethodCallHandler(channel, null),
  );
  test('native channel requires correlated fresh source', () async {
    TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
        .setMockMethodCallHandler(
          channel,
          (call) async => {
            'id': 13,
            'ok': true,
            'discovery_source': 'mac_classic',
            'devices': [],
            'truncated': false,
          },
        );
    final d = MethodChannelClassicDiscovery(channel: channel);
    expect((await d.scan(13))['id'], 13);
    await expectLater(d.scan(14), throwsA(isA<ClassicDiscoveryException>()));
  });
  test(
    'native permission error is actionable, no silent ESP fallback',
    () async {
      TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
          .setMockMethodCallHandler(
            channel,
            (call) async => throw PlatformException(code: 'permission'),
          );
      await expectLater(
        MethodChannelClassicDiscovery(channel: channel).scan(1),
        throwsA(
          isA<ClassicDiscoveryException>().having(
            (e) => e.message,
            'permission',
            contains('Privacy'),
          ),
        ),
      );
    },
  );
  test('native search and selection serialize live-verification source, not ESP scan id', () async {
    final t = FakeTransport();
    final d = _Discovery();
    final c = BottleBleController(
      transport: t,
      sessionStore: MemorySessionStore(),
      classicDiscovery: d,
    );
    try {
      await c.connect(testBottle);
      await c.scanPhones();
      expect(c.phoneDiscoverySource, 'mac_classic');
      expect(t.commands.any((x) => x['cmd'] == 'phone.scan'), isFalse);
      await c.selectPhone('AA:BB:CC:DD:EE:FF', c.phoneScanId!);
      final command = t.commands.singleWhere((x) => x['cmd'] == 'phone.select');
      expect(command['discovery_source'], 'mac_classic');
      expect(command['name'], 'Galaxy 水');
      expect(command.containsKey('scan_id'), isFalse);
    } finally {
      await c.disconnect();
      c.dispose();
    }
  });
  test('native stale inquiry cannot populate or save devices', () async {
    final t = FakeTransport();
    final d = _Discovery()..stale = true;
    final c = BottleBleController(
      transport: t,
      sessionStore: MemorySessionStore(),
      classicDiscovery: d,
    );
    try {
      await c.connect(testBottle);
      await expectLater(c.scanPhones(), throwsA(isA<BottleCommandException>()));
      expect(c.phones, isEmpty);
      expect(c.phoneScanId, isNull);
      expect(c.phoneScanBusy, isFalse);
      expect(t.commands.any((x) => x['cmd'] == 'phone.select'), isFalse);
    } finally {
      await c.disconnect();
      c.dispose();
    }
  });
}
