import 'dart:convert';

/// Protocol v2, shared with docs/PROTOCOL.md. No legacy text commands.
class BottleProtocol {
  static const serviceUuid = '6b6f7474-6c65-4001-b5a3-000000000001';
  static const cmdUuid = '6b6f7474-6c65-4002-b5a3-000000000001';
  static const evtUuid = '6b6f7474-6c65-4003-b5a3-000000000001';
  static const maxFrameBytes = 4096;

  static List<int> encode(int id, String command, Map<String, dynamic> fields) {
    final bytes = utf8.encode(
      '${jsonEncode({...fields, 'id': id, 'cmd': command})}\n',
    );
    if (bytes.length > maxFrameBytes) {
      throw const FormatException('Command exceeds 4096 bytes');
    }
    return bytes;
  }

  static String? validateWifi(String ssid, String password) {
    final length = utf8.encode(ssid).length;
    if (length < 1 || length > 32 || ssid.contains('\u0000')) {
      return 'Network name must be 1–32 UTF-8 bytes (spaces are kept).';
    }
    final passLength = utf8.encode(password).length;
    if ((passLength != 0 && (passLength < 8 || passLength > 63)) ||
        password.contains('\u0000')) {
      return 'Password must be empty for an open network, or 8–63 UTF-8 bytes.';
    }
    return null;
  }

  static const ranges = <String, (int, int)>{
    'start_min': (0, 1439),
    'end_min': (0, 1439),
    'interval_min': (1, 240),
    'beep_type': (0, 3),
    'color': (0, 0xffffff),
    'reminder_color': (0, 0xffffff),
    'missing_color': (0, 0xffffff),
    'missing_sec': (10, 3600),
    'beep_delay_sec': (0, 30),
    'drink_grams': (5, 500),
    'settle_ms': (500, 5000),
    'tz_min': (-720, 840),
  };

  static String? validateSettings(Map<String, dynamic> fields) {
    for (final entry in fields.entries) {
      if (entry.key == 'dnd' || entry.key == 'presence_enabled') {
        if (entry.value is! bool) return '${entry.key} must be on or off.';
        continue;
      }
      final range = ranges[entry.key];
      if (range == null) return 'Unknown setting: ${entry.key}';
      if (entry.value is! int ||
          entry.value < range.$1 ||
          entry.value > range.$2) {
        return '${entry.key} must be ${range.$1}–${range.$2}.';
      }
      if (entry.key == 'tz_min' && entry.value % 15 != 0) {
        return 'Timezone must use 15-minute increments.';
      }
    }
    return null;
  }

  /// Redacts by key recursively, including NVS entries. Never log raw frames or
  /// exception payloads: either could contain credentials from a faulty peer.
  static dynamic sanitized(dynamic value) {
    if (value is Map) {
      final key = value['key']?.toString() ?? '';
      final secretEntry = isSecretKey(key);
      return <String, dynamic>{
        for (final entry in value.entries)
          entry.key.toString():
              isSecretKey(entry.key.toString()) ||
                  (secretEntry && entry.key == 'value')
              ? (entry.value == 'empty' ? 'empty' : '[redacted]')
              : sanitized(entry.value),
      };
    }
    if (value is List) return value.map(sanitized).toList();
    return value;
  }

  static bool isSecretKey(String key) => RegExp(
    r'pass|password|irk|identity|secret|wifi_p\d',
    caseSensitive: false,
  ).hasMatch(key);
}

/// Accumulates bytes, not strings: a multibyte UTF-8 character may cross chunks.
/// Oversized frames are discarded through newline, then parsing recovers.
class JsonLineDecoder {
  JsonLineDecoder({required this.onMessage, required this.onError});
  final void Function(Map<String, dynamic>) onMessage;
  final void Function(String) onError;
  final List<int> _buffer = [];
  bool _discarding = false;

  void reset() {
    _buffer.clear();
    _discarding = false;
  }

  void add(List<int> bytes) {
    for (final byte in bytes) {
      if (byte == 10) {
        if (!_discarding && _buffer.isNotEmpty) {
          try {
            final decoded = jsonDecode(utf8.decode(_buffer));
            if (decoded is! Map<String, dynamic>) {
              throw const FormatException('Object required');
            }
            onMessage(decoded);
          } on FormatException {
            onError('Malformed JSON notification ignored.');
          }
        }
        reset();
      } else if (!_discarding) {
        _buffer.add(byte);
        if (_buffer.length >= BottleProtocol.maxFrameBytes) {
          _buffer.clear();
          _discarding = true;
          onError(
            'Notification exceeded 4096 bytes; discarded through newline.',
          );
        }
      }
    }
  }
}

/// One visible 2.4 GHz network found by the bottle, not a saved credential.
class WifiNetwork {
  const WifiNetwork({
    required this.ssid,
    required this.rssi,
    required this.secured,
  });
  final String ssid;
  final int rssi;
  final bool secured;

  String get signalLabel => rssi >= -60
      ? 'Strong signal'
      : rssi >= -75
      ? 'Fair signal'
      : 'Weak signal';
}

class WifiScanResult {
  WifiScanResult.fromMessage(Map<String, dynamic> message) {
    final entries = message['networks'];
    if (entries is! List ||
        (message['truncated'] != null && message['truncated'] is! bool) ||
        (message['hidden_count'] != null &&
            (message['hidden_count'] is! int || message['hidden_count'] < 0))) {
      throw const FormatException('Invalid Wi-Fi search result');
    }
    final unique = <String, WifiNetwork>{};
    for (final entry in entries) {
      if (entry is! Map ||
          entry['ssid'] is! String ||
          BottleProtocol.validateWifi(entry['ssid'] as String, '') != null ||
          entry['rssi'] is! int ||
          entry['rssi'] < -127 ||
          entry['rssi'] > 0 ||
          entry['secured'] is! bool) {
        throw const FormatException('Invalid Wi-Fi network');
      }
      final network = WifiNetwork(
        ssid: entry['ssid'] as String,
        rssi: entry['rssi'] as int,
        secured: entry['secured'] as bool,
      );
      final previous = unique[network.ssid];
      if (previous == null || network.rssi > previous.rssi) {
        unique[network.ssid] = network;
      }
    }
    final sorted = unique.values.toList()
      ..sort((a, b) {
        final signal = b.rssi.compareTo(a.rssi);
        return signal == 0 ? a.ssid.compareTo(b.ssid) : signal;
      });
    networks = List.unmodifiable(sorted.take(15));
    truncated = message['truncated'] == true || sorted.length > 15;
    hiddenCount = message['hidden_count'] as int? ?? 0;
  }

  late final List<WifiNetwork> networks;
  late final bool truncated;
  late final int hiddenCount;
}

class BottleStatus {
  BottleStatus(Map<String, dynamic> value) : data = Map.unmodifiable(value);
  final Map<String, dynamic> data;
  Map<String, dynamic> get settings => Map<String, dynamic>.from(
    data['settings'] is Map ? data['settings'] : {},
  );
  List<Map<String, dynamic>> get networks => (data['networks'] is List
      ? (data['networks'] as List)
            .whereType<Map>()
            .map((e) => Map<String, dynamic>.from(e))
            .toList()
      : []);
  String text(String key, [String fallback = 'Unknown']) =>
      data[key]?.toString() ?? fallback;
  bool flag(String key) => data[key] == true;
  num? number(String key) => data[key] is num ? data[key] as num : null;
}

class NvsEntry {
  const NvsEntry(this.key, this.value, this.stored);
  final String key;
  final dynamic value;
  final bool stored;
  static const requiredKeys = {'tare_offset', 'cal_factor'};
  static const optionalKeys = {
    'baseline_weight',
    'ble_mac',
    'ble_irk',
    'sta_ssid',
    'sta_pass',
    'ap_ssid',
    'ap_pass',
    'ap_behavior',
    'missing_grams',
    'present_grams',
  };
  static const expectedKeys = [
    'schema',
    'tare_offset',
    'cal_factor',
    'baseline_weight',
    'daily_total',
    'last_day',
    'ble_mac',
    'ble_irk',
    'dnd',
    'color',
    'reminder_color',
    'missing_color',
    'start_min',
    'end_min',
    'interval_min',
    'beep_type',
    'tz_min',
    'missing_sec',
    'beep_delay_sec',
    'drink_grams',
    'settle_ms',
    'presence_enabled',
    'net_count',
    'wifi_s0',
    'wifi_p0',
    'wifi_s1',
    'wifi_p1',
    'wifi_s2',
    'wifi_p2',
    'wifi_s3',
    'wifi_p3',
    'sta_ssid',
    'sta_pass',
    'ap_ssid',
    'ap_pass',
    'ap_behavior',
    'missing_grams',
    'present_grams',
  ];
  bool get requiredMissing =>
      requiredKeys.contains(key) &&
      (!stored || value == null || (key == 'cal_factor' && value == 0));
  String get classification {
    if (requiredMissing) return 'Setup required';
    if (stored) return 'Stored';
    if (optionalKeys.contains(key) || key.startsWith('wifi_')) {
      return 'Optional / legacy';
    }
    return value == null ? 'Not reported' : 'Firmware default';
  }

  String get displayValue {
    if (BottleProtocol.isSecretKey(key)) {
      return value == 'empty' || value == null ? 'empty' : 'set';
    }
    return value?.toString() ?? '—';
  }
}
