import 'dart:convert';

import 'package:bottle_tester/bottle_protocol.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  test(
    'UTF-8/newline framing supports split characters and multiple messages',
    () {
      final messages = <Map<String, dynamic>>[];
      final errors = <String>[];
      final decoder = JsonLineDecoder(
        onMessage: messages.add,
        onError: errors.add,
      );
      final bytes = utf8.encode(
        '{"ssid":"Café room"}\n{"type":"ack","id":1,"ok":true}\r\n',
      );
      for (final byte in bytes) {
        decoder.add([byte]);
      }
      expect(messages.length, 2);
      expect(messages.first['ssid'], 'Café room');
      expect(errors, isEmpty);
    },
  );
  test(
    'malformed, nonobject, invalid UTF-8 and oversized frames recover safely',
    () {
      final messages = <Map<String, dynamic>>[];
      final errors = <String>[];
      final decoder = JsonLineDecoder(
        onMessage: messages.add,
        onError: errors.add,
      );
      decoder.add(utf8.encode('not JSON password=secret\n[]\n'));
      decoder.add([0xff, 10]);
      decoder.add(List.filled(4200, 65));
      decoder.add(utf8.encode('\n{"ok":true}\n'));
      expect(errors.length, 4);
      expect(errors.join(), isNot(contains('secret')));
      expect(messages, [
        {'ok': true},
      ]);
    },
  );
  test(
    'partial frames wait for newline and reset discards old session bytes',
    () {
      final messages = <Map<String, dynamic>>[];
      final decoder = JsonLineDecoder(onMessage: messages.add, onError: (_) {});
      decoder.add(utf8.encode('{"ok":true}'));
      expect(messages, isEmpty);
      decoder.reset();
      decoder.add(utf8.encode('{"new":1}\n'));
      expect(messages.single, {'new': 1});
    },
  );
  test('JSON encoding preserves Wi-Fi spaces, quotes and Unicode', () {
    final bytes = BottleProtocol.encode(9, 'wifi.add', {
      'ssid': ' Living room Café ',
      'password': 'a b "c" def',
    });
    expect(bytes.last, 10);
    final decoded = jsonDecode(utf8.decode(bytes)) as Map;
    expect(decoded['ssid'], ' Living room Café ');
    expect(decoded['password'], 'a b "c" def');
    expect(decoded['id'], 9);
  });
  test(
    'multi-chunk Wi-Fi scan frame preserves exact UTF-8 names until newline',
    () {
      final messages = <Map<String, dynamic>>[];
      final errors = <String>[];
      final decoder = JsonLineDecoder(
        onMessage: messages.add,
        onError: errors.add,
      );
      final bytes = utf8.encode(
        jsonEncode({
          'type': 'wifi.scan',
          'id': 123,
          'ok': true,
          'networks': List.generate(
            15,
            (i) => {
              'ssid': ' Café 水 $i ',
              'rssi': -30 - i,
              'secured': i.isEven,
            },
          ),
          'truncated': true,
          'hidden_count': 2,
        }),
      );
      expect(bytes.length, greaterThan(20));
      for (var i = 0; i < bytes.length; i += 7) {
        decoder.add(
          bytes.sublist(i, i + 7 > bytes.length ? bytes.length : i + 7),
        );
      }
      expect(messages, isEmpty);
      decoder.add([10]);
      final scan = WifiScanResult.fromMessage(messages.single);
      expect(scan.networks.length, 15);
      expect(scan.networks.first.ssid, ' Café 水 0 ');
      expect(scan.networks.last.ssid, ' Café 水 14 ');
      expect(scan.truncated, isTrue);
      expect(scan.hiddenCount, 2);
      expect(errors, isEmpty);
    },
  );
  test('scan models sort, de-duplicate strongest and bound results without trimming', () {
    final scan = WifiScanResult.fromMessage({
      'networks': [
        ...List.generate(
          18,
          (i) => {'ssid': 'Room $i', 'rssi': -90 + i, 'secured': false},
        ),
        {'ssid': ' Café 水 ', 'rssi': -30, 'secured': true},
        {'ssid': ' Café 水 ', 'rssi': -80, 'secured': false},
      ],
    });
    expect(scan.networks.length, 15);
    expect(scan.networks.first.ssid, ' Café 水 ');
    expect(scan.networks.first.rssi, -30);
    expect(scan.networks.first.secured, isTrue);
    expect(scan.truncated, isTrue);
    expect(scan.hiddenCount, 0);
    expect(scan.networks.first.signalLabel, 'Strong signal');
    expect(
      const WifiNetwork(ssid: 'x', rssi: -70, secured: false).signalLabel,
      'Fair signal',
    );
    expect(scan.networks.last.signalLabel, 'Weak signal');
    for (final invalid in [
      {'networks': null},
      {
        'networks': [
          {'ssid': '', 'rssi': -50, 'secured': true},
        ],
      },
      {
        'networks': [
          {'ssid': 'x', 'rssi': 'bad', 'secured': true},
        ],
      },
      {'networks': <dynamic>[], 'hidden_count': -1},
    ]) {
      expect(() => WifiScanResult.fromMessage(invalid), throwsFormatException);
    }
  });
  test('Wi-Fi credential validation counts bytes, accepts open networks', () {
    expect(BottleProtocol.validateWifi('Room WiFi', 'a b c d e'), isNull);
    expect(BottleProtocol.validateWifi(' ', ''), isNull);
    expect(BottleProtocol.validateWifi('', ''), isNotNull);
    expect(BottleProtocol.validateWifi('a' * 33, ''), isNotNull);
    expect(BottleProtocol.validateWifi('é' * 17, ''), isNotNull);
    expect(BottleProtocol.validateWifi('ok', '1234567'), isNotNull);
    expect(BottleProtocol.validateWifi('ok', 'é' * 32), isNotNull);
    expect(BottleProtocol.validateWifi('ok', 'x' * 64), isNotNull);
    expect(BottleProtocol.validateWifi('ok', 'abcd\u0000abcd'), isNotNull);
  });
  test(
    'settings validate all ranges, booleans, timezone and unknown fields',
    () {
      for (final entry in BottleProtocol.ranges.entries) {
        final minimum = entry.value.$1;
        final maximum = entry.value.$2;
        expect(BottleProtocol.validateSettings({entry.key: minimum}), isNull);
        expect(BottleProtocol.validateSettings({entry.key: maximum}), isNull);
        expect(
          BottleProtocol.validateSettings({entry.key: minimum - 1}),
          isNotNull,
        );
        expect(
          BottleProtocol.validateSettings({entry.key: maximum + 1}),
          isNotNull,
        );
      }
      expect(BottleProtocol.validateSettings({'drink_grams': 15.5}), isNotNull);
      expect(BottleProtocol.validateSettings({'tz_min': 331}), isNotNull);
      expect(BottleProtocol.validateSettings({'dnd': 1}), isNotNull);
      expect(BottleProtocol.validateSettings({'dnd': false}), isNull);
      expect(BottleProtocol.validateSettings({'dnd': true}), isNull);
      expect(BottleProtocol.validateSettings({'unexpected': 3}), isNotNull);
    },
  );
  test('presence_enabled is read-only even when writing true', () {
    for (final value in [false, true, 1, null]) {
      expect(
        BottleProtocol.validateSettings({'presence_enabled': value}),
        'Unknown setting: presence_enabled',
      );
    }
    final status = BottleStatus({
      'settings': {'presence_enabled': true},
    });
    expect(status.settings['presence_enabled'], isTrue);
  });
  test('NVS defaults and optional legacy keys are not faults', () {
    expect(
      const NvsEntry('start_min', 600, false).classification,
      'Firmware default',
    );
    expect(const NvsEntry('interval_min', 30, false).requiredMissing, isFalse);
    expect(
      const NvsEntry('ble_mac', null, false).classification,
      'Optional / legacy',
    );
    expect(const NvsEntry('wifi_s3', null, false).requiredMissing, isFalse);
    expect(const NvsEntry('cal_factor', null, false).requiredMissing, isTrue);
    expect(const NvsEntry('cal_factor', 0, true).requiredMissing, isTrue);
    expect(const NvsEntry('tare_offset', 0, true).requiredMissing, isFalse);
    expect(const NvsEntry('ble_irk', 'secret', true).displayValue, 'set');
  });
  test('sanitization recursively removes all password and identity values', () {
    final output = jsonEncode(
      BottleProtocol.sanitized({
        'password': 'my secret',
        'nested': {'wifi_p2': 'another secret'},
        'nvs': {'key': 'ble_irk', 'value': 'key bytes', 'stored': true},
        'ssid': 'Normal network',
      }),
    );
    expect(output, isNot(contains('my secret')));
    expect(output, isNot(contains('another secret')));
    expect(output, isNot(contains('key bytes')));
    expect(output, contains('Normal network'));
  });
}
