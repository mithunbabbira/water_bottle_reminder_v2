import 'package:bottle_tester/bottle_ble_controller.dart';
import 'package:bottle_tester/bottle_protocol.dart';
import 'package:flutter_test/flutter_test.dart';

import 'fakes.dart';

const address = 'AA:BB:CC:DD:EE:FF';
Map<String, dynamic> scan(int id, {List<dynamic>? devices}) => {
  'type': 'phone.scan',
  'id': id,
  'ok': true,
  'truncated': false,
  'devices':
      devices ??
      [
        {'address': address, 'name': 'Galaxy 水', 'rssi': -40},
      ],
};
Map<String, dynamic> probe(int id, {bool present = true, String? reason}) => {
  'type': 'phone.probe',
  'id': id,
  'ok': true,
  'present': present,
  'reason': reason ?? (present ? 'reply' : 'no_reply'),
  'elapsed_ms': 200,
};

void main() {
  test('Classic UTF-8 validation, strongest duplicates, deterministic bounded 8', () {
    final entries = List.generate(
      10,
      (i) => {
        'address':
            'AA:BB:CC:DD:EE:${i.toRadixString(16).padLeft(2, '0').toUpperCase()}',
        'name': i == 0 ? '' : '水' * 16,
        'rssi': -80 + i,
      },
    );
    entries.add({...entries[0], 'rssi': -20});
    final result = PhoneScanResult.fromMessage(scan(1, devices: entries));
    expect(result.devices.length, 8);
    expect(result.truncated, isTrue);
    expect(result.devices.first.rssi, -20);
    expect(result.devices.first.label, 'Unnamed Classic device');
    expect(
      result.devices.map((p) => p.rssi),
      orderedEquals([-20, -71, -72, -73, -74, -75, -76, -77]),
    );
    for (final bad in [
      {'address': '', 'name': '', 'rssi': -40},
      {'address': address, 'name': '水' * 17, 'rssi': -40},
      {'address': address, 'name': '\ud800', 'rssi': -40},
      {'address': address, 'name': 'bad\u0000', 'rssi': -40},
      {'address': address, 'name': '', 'rssi': 1},
      {'address': '00:00:00:00:00:00', 'name': '', 'rssi': -40},
    ]) {
      expect(
        () => PhoneScanResult.fromMessage(scan(1, devices: [bad])),
        throwsFormatException,
      );
    }
    expect(PhoneScanResult.fromMessage(scan(1, devices: [])).devices, isEmpty);
  });

  late FakeTransport transport;
  late BottleBleController c;
  setUp(() async {
    transport = FakeTransport();
    c = BottleBleController(
      transport: transport,
      sessionStore: MemorySessionStore(),
      pollInterval: Duration.zero,
      phoneTimeout: const Duration(milliseconds: 70),
    );
    await c.connect(testBottle);
  });
  tearDown(() => c.dispose());
  Future<void> written() =>
      Future<void>.delayed(const Duration(milliseconds: 5));

  for (final resultFirst in [true, false]) {
    for (final command in ['phone.scan', 'phone.probe']) {
      test(
        '$command needs matching ACK + result in either order ($resultFirst)',
        () async {
          transport.autoRespond = false;
          var complete = false;
          final future =
              (command == 'phone.scan' ? c.scanPhones() : c.probePhone()).then(
                (_) => complete = true,
              );
          await written();
          final id = transport.commands.last['id'] as int;
          final result = command == 'phone.scan' ? scan(id) : probe(id);
          transport.emit({...result, 'id': id + 100});
          transport.emit({'type': 'ack', 'id': id + 100, 'ok': true});
          await written();
          expect(complete, isFalse);
          expect(
            c.presenceState,
            command == 'phone.probe' ? 'checking' : 'present',
          );
          if (resultFirst) {
            transport.emit(result);
          } else {
            transport.emit({'type': 'ack', 'id': id, 'ok': true});
          }
          await written();
          expect(complete, isFalse);
          expect(c.phoneScanId, isNull);
          if (resultFirst) {
            transport.emit({'type': 'ack', 'id': id, 'ok': true});
          } else {
            transport.emit(result);
          }
          await future;
          if (command == 'phone.scan') {
            expect(c.phoneScanId, id);
            expect(c.phones.single.name, 'Galaxy 水');
          } else {
            expect(c.presenceState, 'present');
          }
        },
      );
    }
  }

  test('selection sends only current address + scan_id, reads back; replace clears presence', () async {
    await c.scanPhones();
    final scanId = c.phoneScanId!;
    await c.selectPhone('22:33:44:55:66:77', scanId);
    expect(transport.commands[transport.commands.length - 2], {
      'cmd': 'phone.select',
      'id': transport.commands[transport.commands.length - 2]['id'],
      'address': '22:33:44:55:66:77',
      'scan_id': scanId,
    });
    expect(transport.commands.last['cmd'], 'status');
    expect(c.snapshot!.text('phone_mac'), '22:33:44:55:66:77');
    expect(c.presenceState, 'checking');
    await c.clearPhone();
    expect(c.presenceState, 'not_configured');
    expect(c.snapshot!.data['phone_mac'], isNull);
    expect(c.snapshot!.text('app_peer_mac'), '11:22:33:44:55:66');
  });

  test(
    'empty malformed stale or undiscovered selection rejected without a write',
    () async {
      await c.scanPhones();
      final id = c.phoneScanId!;
      final count = transport.commands.length;
      for (final candidate in [
        '',
        'bad',
        '00:00:00:00:00:00',
        '99:88:77:66:55:44',
      ]) {
        await expectLater(
          c.selectPhone(candidate, id),
          throwsA(isA<BottleCommandException>()),
        );
      }
      await expectLater(
        c.selectPhone(address, id - 1),
        throwsA(isA<BottleCommandException>()),
      );
      expect(transport.commands.length, count);
      await c.scanPhones();
      await expectLater(
        c.selectPhone(address, id),
        throwsA(isA<BottleCommandException>()),
      );
      await c.disconnect();
      expect(c.phoneScanId, isNull);
      expect(c.phones, isEmpty);
    },
  );

  test(
    'negative selection ACK and mismatched readback never claim saved/present',
    () async {
      await c.scanPhones();
      transport.onCommand = (cmd) => transport.respond(
        cmd,
        ok: cmd['cmd'] != 'phone.select',
        error: 'stale scan',
      );
      await expectLater(
        c.selectPhone(address, c.phoneScanId!),
        throwsA(isA<BottleCommandException>()),
      );
      expect(c.presenceState, 'unknown');
      expect(c.bannerError, contains('stale scan'));
      transport.onCommand = (cmd) {
        if (cmd['cmd'] == 'phone.select') {
          transport.emit({'type': 'ack', 'id': cmd['id'], 'ok': true});
        } else {
          transport.respond(cmd);
        }
      };
      await expectLater(
        c.selectPhone('22:33:44:55:66:77', c.phoneScanId!),
        throwsA(isA<BottleCommandException>()),
      );
      expect(c.bannerError, contains('readback did not match'));
      expect(c.presenceState, 'unknown');
    },
  );

  for (final cmd in ['phone.scan', 'phone.probe']) {
    for (final failure in ['negative', 'malformed', 'timeout', 'disconnect']) {
      test('$cmd $failure fails safely and cleans up', () async {
        transport.autoRespond = false;
        final future = cmd == 'phone.scan' ? c.scanPhones() : c.probePhone();
        final expected = expectLater(
          future,
          throwsA(isA<BottleCommandException>()),
        );
        await written();
        final id = transport.commands.last['id'] as int;
        if (failure == 'negative') {
          // Even a premature successful result cannot override a negative ACK.
          transport.emit(cmd == 'phone.scan' ? scan(id) : probe(id));
          transport.emit({
            'type': 'ack',
            'id': id,
            'ok': false,
            'error': 'busy',
          });
        } else if (failure == 'malformed') {
          transport.emit({'type': 'ack', 'id': id, 'ok': true});
          transport.emit({'type': cmd, 'id': id, 'ok': true});
        } else if (failure == 'disconnect') {
          transport.loseConnection();
        } else {
          transport.emit({'type': 'ack', 'id': id, 'ok': true});
        }
        await expected;
        await written(); // Lost-link reporting completes after transport cleanup.
        expect(c.phoneScanBusy, isFalse);
        expect(c.phoneProbeBusy, isFalse);
        expect(c.phoneScanId, isNull);
        if (cmd == 'phone.probe') expect(c.presenceState, isNot('present'));
        expect(c.bannerError, isNotNull);
        if (failure != 'disconnect') {
          transport.autoRespond = true;
          await c.refreshStatus();
        }
      });
    }
  }

  test('away and radio timeout are real negative outcomes, never optimistic present', () async {
    for (final reason in ['no_reply', 'timeout', 'radio_error']) {
      transport.onCommand = (cmd) {
        transport.emit({'type': 'ack', 'id': cmd['id'], 'ok': true});
        transport.emit(probe(cmd['id'], present: false, reason: reason));
      };
      await c.probePhone();
      expect(c.presenceState, reason == 'no_reply' ? 'away' : reason);
      if (reason != 'no_reply') expect(c.bannerError, contains(reason));
    }
  });

  test(
    'empty Classic discovery stays unselected and invalidates previous scan',
    () async {
      await c.scanPhones();
      final previous = c.phoneScanId!;
      transport.onCommand = (cmd) {
        transport.emit(scan(cmd['id'], devices: []));
        transport.emit({'type': 'ack', 'id': cmd['id'], 'ok': true});
      };
      await c.scanPhones();
      expect(c.phoneScanId, isNot(previous));
      expect(c.phones, isEmpty);
      await expectLater(
        c.selectPhone(address, c.phoneScanId!),
        throwsA(isA<BottleCommandException>()),
      );
      expect(
        transport.commands.where((cmd) => cmd['cmd'] == 'phone.select'),
        isEmpty,
      );
    },
  );

  test('fresh status replaces manual result; expired or wrong-target probe cannot show present', () async {
    await c.probePhone();
    expect(c.presenceState, 'present');
    transport.status['phone_present'] = false;
    transport.status['presence_state'] = 'away';
    await c.refreshStatus();
    expect(c.presenceState, 'away');
    transport.status['phone_present'] = true;
    transport.status['presence_state'] = 'present';
    transport.status['presence_last_check_age_sec'] = 30;
    await c.refreshStatus();
    expect(c.presenceState, 'unknown');
    transport.onCommand = (cmd) {
      transport.emit({...transport.status, 'phone_mac': '22:33:44:55:66:77'});
      transport.emit({'type': 'ack', 'id': cmd['id'], 'ok': true});
      transport.emit(probe(cmd['id']));
    };
    await expectLater(c.probePhone(), throwsA(isA<BottleCommandException>()));
    expect(c.presenceState, 'unknown');
    expect(c.bannerError, contains('changed during check'));
  });

  test('legacy BLE status cannot establish Classic presence or bypass readonly policy', () async {
    transport.status.remove('presence_method');
    await c.refreshStatus();
    expect(c.presenceState, 'unknown');
    final count = transport.commands.length;
    await expectLater(
      c.saveSettings({'presence_enabled': true}),
      throwsA(isA<BottleCommandException>()),
    );
    await expectLater(
      c.saveSettings({'presence_enabled': false}),
      throwsA(isA<BottleCommandException>()),
    );
    await expectLater(c.probePhone(), throwsA(isA<BottleCommandException>()));
    expect(transport.commands.length, count);
    expect(
      const NvsEntry('classic_mac', null, false).classification,
      contains('Required for alerts'),
    );
    expect(const NvsEntry('ble_mac', null, false).requiredMissing, isFalse);
  });
}
