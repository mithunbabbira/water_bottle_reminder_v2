import 'dart:async';
import 'dart:convert';
import 'dart:math' as math;

import 'package:flutter/foundation.dart';

import 'bottle_protocol.dart';
import 'bottle_transport.dart';

export 'bottle_transport.dart' show ScannedBottle;

class BottleCommandException implements Exception {
  const BottleCommandException(this.message);
  final String message;
  @override
  String toString() => message;
}

class _Request {
  _Request(this.id, this.command);
  final int id;
  final String command;
  final done = Completer<void>();
  bool acknowledged = false;
  bool receivedStatus = false;
  bool nvsEnded = false;
  bool wifiScanReceived = false;
  WifiScanResult? wifiScan;
  String? wifiScanError;
  void maybeComplete() {
    if (!done.isCompleted &&
        acknowledged &&
        (command != 'status' || receivedStatus) &&
        (command != 'nvs' || nvsEnded) &&
        (command != 'wifi.scan' || wifiScanReceived)) {
      if (wifiScanError != null) {
        fail(wifiScanError!);
      } else {
        done.complete();
      }
    }
  }

  void fail(String message) {
    if (!done.isCompleted) done.completeError(BottleCommandException(message));
  }
}

class BottleBleController extends ChangeNotifier {
  BottleBleController({
    BottleTransport? transport,
    SessionStore? sessionStore,
    this.ackTimeout = const Duration(seconds: 15),
    this.nvsTimeout = const Duration(seconds: 30),
    this.scanTimeout = const Duration(seconds: 25),
    this.pollInterval = const Duration(seconds: 2),
    this.reconnectDelay = const Duration(seconds: 2),
  }) : _transport = transport ?? FlutterBottleTransport(),
       _store = sessionStore ?? PreferencesSessionStore() {
    _decoder = JsonLineDecoder(onMessage: _onMessage, onError: reportError);
    _subscriptions.add(
      _transport.notifications.listen(
        _decoder.add,
        onError: (_) => reportError(
          'Bluetooth notifications failed. Reconnect to continue.',
        ),
      ),
    );
    _subscriptions.add(
      _transport.disconnections.listen((_) => unawaited(_lostConnection())),
    );
    _subscriptions.add(
      _transport.discoveries.listen(
        (found) {
          devices
            ..clear()
            ..addAll(found);
          _notify();
        },
        onError: (_) => reportError(
          'Could not scan. Check Bluetooth permissions and try again.',
        ),
      ),
    );
  }

  final BottleTransport _transport;
  final SessionStore _store;
  final Duration ackTimeout;
  final Duration nvsTimeout;
  final Duration scanTimeout;
  final Duration pollInterval;
  final Duration reconnectDelay;
  final List<StreamSubscription<dynamic>> _subscriptions = [];
  late final JsonLineDecoder _decoder;
  final List<ScannedBottle> devices = [];
  final List<String> log = [];
  final List<String> events = [];
  final Map<String, NvsEntry> nvsEntries = {};
  final Set<String> _secrets = {};
  BottleStatus? snapshot;
  ScannedBottle? connected;
  ScannedBottle? remembered;
  bool scanning = false;
  bool connecting = false;
  bool nvsLoading = false;
  bool nvsComplete = false;
  bool scanBusy = false;
  bool scanComplete = false;
  String? scanError;
  List<WifiNetwork> wifiNetworks = const [];
  bool scanTruncated = false;
  int scanHiddenCount = 0;
  bool get wifiTimeBusy =>
      const {'connecting', 'syncing'}.contains(snapshot?.text('wifi_state'));
  bool rememberConnection = false;
  String connectionLabel = 'Not connected';
  String? bannerError;
  String? notice;
  String? nvsError;
  DateTime? lastStatusAt;
  bool _disposed = false;
  bool _initialized = false;
  bool _manualDisconnect = false;
  bool _reconnectAttempted = false;
  int _generation = 0;
  int _nextId = 1;
  int _pending = 0;
  Future<void>? _queue;
  _Request? _request;
  Timer? _poll;
  Timer? _reconnect;
  bool get busy => _pending > 0 || connecting || scanBusy;
  bool get isConnected => connected != null;
  bool get stale =>
      isConnected &&
      (lastStatusAt == null ||
          DateTime.now().difference(lastStatusAt!) >
              const Duration(seconds: 6));
  int get mtu => _transport.mtu;
  List<String> get nvsRequiredMissing => nvsEntries.values
      .where((e) => e.requiredMissing)
      .map((e) => e.key)
      .toList();
  List<String> get nvsUnreported => NvsEntry.expectedKeys
      .where((key) => !nvsEntries.containsKey(key))
      .toList();

  Future<void> initialize() async {
    if (_initialized || _disposed) return;
    _initialized = true;
    final generation = _generation;
    try {
      final saved = await _store.load();
      if (_disposed || generation != _generation) return;
      remembered = saved;
      rememberConnection = saved != null;
      if (saved != null) {
        _reconnectAttempted = true;
        await connect(saved, reconnect: true);
      }
    } catch (_) {
      reportError(
        'Could not restore the saved bottle. You can connect manually.',
      );
    }
  }

  void clearBanner() {
    bannerError = null;
    _notify();
  }

  String safeText(String text) {
    var result = text;
    for (final secret in _secrets) {
      if (secret.isNotEmpty) result = result.replaceAll(secret, '[redacted]');
    }
    return result.length > 240 ? '${result.substring(0, 240)}…' : result;
  }

  void reportError(String message) {
    if (_disposed) return;
    bannerError = safeText(message);
    notice = null;
    _appendLog('Error: $bannerError');
    _notify();
  }

  Future<void> startScan() async {
    if (scanning || connecting || isConnected || _disposed) return;
    scanning = true;
    devices.clear();
    clearBanner();
    _notify();
    try {
      await _transport.scan();
      if (devices.isEmpty && !_disposed) {
        notice =
            'No bottles found. Power your bottle, move closer, and try again.';
      }
    } catch (_) {
      reportError(
        'Could not scan. Turn Bluetooth on and allow Bluetooth access, then retry.',
      );
    } finally {
      scanning = false;
      _notify();
    }
  }

  Future<void> stopScan() async {
    try {
      await _transport.stopScan();
    } catch (_) {}
    scanning = false;
    _notify();
  }

  Future<void> setRememberConnection(bool value) async {
    rememberConnection = value;
    remembered = value ? connected : null;
    _notify();
    try {
      await _store.save(remembered);
    } catch (_) {
      reportError('Could not save reconnect preference.');
    }
  }

  /// Called only after an explicit selection/confirmation, or for an opted-in
  /// exact last device. Scanning never connects or changes the bottle's peer.
  Future<void> connect(ScannedBottle device, {bool reconnect = false}) async {
    if (connecting || isConnected || _disposed) return;
    _reconnect?.cancel();
    _manualDisconnect = false;
    if (!reconnect) _reconnectAttempted = false;
    final generation = ++_generation;
    connecting = true;
    connectionLabel = reconnect ? 'Reconnecting once…' : 'Connecting…';
    bannerError = null;
    notice = null;
    snapshot = null;
    lastStatusAt = null;
    nvsEntries.clear();
    nvsComplete = false;
    _resetWifiScan();
    _decoder.reset();
    _notify();
    try {
      await stopScan();
      if (_disposed || generation != _generation) return;
      await _transport.connect(device.id);
      if (_disposed || generation != _generation) return;
      connected = device;
      connectionLabel = 'Connected';
      _appendLog('Connected; protocol handshake');
      await sendCommand('status');
      if (snapshot?.number('protocol') != 2) {
        throw const BottleCommandException(
          'This bottle needs protocol v2 firmware.',
        );
      }
      if (_disposed || generation != _generation) return;
      if (rememberConnection) {
        remembered = device;
        try {
          await _store.save(device);
        } catch (_) {
          reportError(
            'Connected, but reconnect preference could not be saved.',
          );
        }
      }
      if (pollInterval > Duration.zero) {
        _poll = Timer.periodic(pollInterval, (_) {
          _notify();
          // Firmware pushes every 2 s; request a snapshot if two pushes were lost.
          if (!busy &&
              isConnected &&
              (lastStatusAt == null ||
                  DateTime.now().difference(lastStatusAt!) >=
                      pollInterval * 2)) {
            unawaited(sendCommand('status').catchError((Object _) {}));
          }
        });
      }
    } catch (error) {
      if (generation == _generation && !_disposed) {
        await _cleanupConnection();
        connectionLabel = 'Connection failed';
        reportError(
          error is BottleCommandException ? error.message : 'Could not connect. Check bottle power, Bluetooth permission, and protocol v2 firmware.',
        );
      }
    } finally {
      if (generation == _generation) connecting = false;
      _notify();
    }
  }

  Future<void> disconnect() async {
    _manualDisconnect = true;
    _reconnect?.cancel();
    ++_generation;
    connecting = false;
    await _cleanupConnection();
    connectionLabel = 'Disconnected';
    _notify();
  }

  Future<void> _cleanupConnection() async {
    _poll?.cancel();
    _poll = null;
    connected = null;
    snapshot = null;
    lastStatusAt = null;
    final searching = scanBusy;
    _request?.fail(
      _request?.command == 'wifi.scan'
          ? 'Wi-Fi search stopped because Bluetooth disconnected. Reconnect and search again.'
          : 'Connection closed. Changes may not have been saved; read status after reconnecting.',
    );
    _resetWifiScan();
    if (searching) {
      scanError = 'Wi-Fi search stopped because Bluetooth disconnected. Reconnect and search again.';
    }
    _decoder.reset();
    nvsLoading = false;
    try {
      await _transport.disconnect();
    } catch (_) {}
  }

  Future<void> _lostConnection() async {
    if (_disposed || _manualDisconnect || (!isConnected && !connecting)) return;
    final last = connected;
    final generation = ++_generation;
    connecting = false;
    await _cleanupConnection();
    connectionLabel = 'Disconnected';
    reportError('Connection lost. Live values are unavailable.');
    if (last != null &&
        rememberConnection &&
        remembered?.id == last.id &&
        !_reconnectAttempted) {
      _reconnectAttempted = true;
      notice =
          'Trying your saved bottle once. No other bottle will be connected.';
      _reconnect = Timer(reconnectDelay, () {
        if (!_disposed && !_manualDisconnect && generation == _generation) {
          unawaited(connect(last, reconnect: true));
        }
      });
    }
    _notify();
  }

  Future<void> sendCommand(
    String command, [
    Map<String, dynamic> fields = const {},
  ]) {
    final generation = _generation;
    final data = Map<String, dynamic>.from(fields);
    final secret = data['password'];
    if (secret is String && secret.isNotEmpty) _secrets.add(secret);
    _pending++;
    _notify();
    final task = (_queue ?? Future<void>.value()).then((_) async {
      if (_disposed || generation != _generation || !isConnected) {
        throw const BottleCommandException('Connect to your bottle first.');
      }
      final request = _Request(_nextId++, command);
      final payload = BottleProtocol.encode(request.id, command, data);
      _request = request;
      // Install error handling before the first write (a synchronous ACK is valid).
      final completion = request.done.future.timeout(
        command == 'wifi.scan'
            ? scanTimeout
            : command == 'nvs'
            ? nvsTimeout
            : ackTimeout,
        onTimeout: () => throw BottleCommandException(
          command == 'wifi.scan'
              ? 'Wi-Fi search timed out. Move the bottle closer to your 2.4 GHz router and retry.'
              : 'Bottle did not acknowledge in time. Changes are unconfirmed. Refresh or reconnect before retrying.',
        ),
      );
      unawaited(completion.catchError((Object _) {}));
      _appendLog(
        '→ #${request.id} ${jsonEncode(BottleProtocol.sanitized({'cmd': command, ...data}))}',
      );
      try {
        final size = math.max(1, math.min(20, _transport.mtu - 3));
        for (var offset = 0; offset < payload.length; offset += size) {
          if (generation != _generation || _disposed || !isConnected) {
            throw const BottleCommandException(
              'Connection closed during write.',
            );
          }
          await _transport
              .write(
                payload.sublist(
                  offset,
                  math.min(offset + size, payload.length),
                ),
              )
              .timeout(ackTimeout);
        }
        await completion;
        if (command == 'wifi.scan' &&
            generation == _generation &&
            isConnected &&
            !_disposed) {
          // Publish atomically only after BOTH this request's ACK and result.
          final scan = request.wifiScan!;
          wifiNetworks = scan.networks;
          scanTruncated = scan.truncated;
          scanHiddenCount = scan.hiddenCount;
          scanComplete = true;
          _appendLog(
            '← Wi-Fi search complete (${wifiNetworks.length} networks)',
          );
        }
      } on BottleCommandException {
        rethrow;
      } on TimeoutException {
        throw BottleCommandException(
          command == 'wifi.scan'
              ? 'Wi-Fi search could not start: Bluetooth write timed out. Reconnect and retry.'
              : 'Bluetooth write timed out. Reconnect before retrying.',
        );
      } catch (_) {
        throw BottleCommandException(
          command == 'wifi.scan'
              ? 'Wi-Fi search could not start. Reconnect Bluetooth and retry.'
              : 'Bluetooth write failed. Changes are unconfirmed; reconnect and read status.',
        );
      } finally {
        if (identical(_request, request)) _request = null;
        // Complete orphaned futures when a write failed before an ACK arrived.
        if (!request.done.isCompleted) request.done.complete();
      }
    });
    late final Future<void> tail;
    final result = task
        .catchError((Object error) {
          if (!_disposed && generation == _generation) {
            reportError(
              error is BottleCommandException
                  ? error.message
                  : 'Command could not be sent.',
            );
          }
          throw error;
        })
        .whenComplete(() {
          _pending--;
          if (identical(_queue, tail)) _queue = null;
          _notify();
        });
    tail = result.then((_) {}, onError: (Object _) {});
    _queue = tail;
    return result;
  }

  Future<void> refreshStatus() => sendCommand('status');
  Future<void> saveSettings(Map<String, dynamic> values) async {
    final error = BottleProtocol.validateSettings(values);
    if (error != null) return _reject(error);
    await _mutate('settings', values);
  }

  Future<void> syncTime({int? timezoneMinutes}) async {
    final now = DateTime.now();
    final tz = timezoneMinutes ?? now.timeZoneOffset.inMinutes;
    final error = BottleProtocol.validateSettings({'tz_min': tz});
    if (error != null) return _reject(error);
    await _mutate('time.set', {
      'epoch': now.millisecondsSinceEpoch ~/ 1000,
      'tz_min': tz,
    });
  }

  void _resetWifiScan() {
    scanBusy = false;
    scanComplete = false;
    scanError = null;
    wifiNetworks = const [];
    scanTruncated = false;
    scanHiddenCount = 0;
  }

  /// Discovery only: never saves credentials, connects Wi-Fi, or starts NTP.
  Future<void> scanWifi() async {
    if (!isConnected) {
      return _reject('Connect to your bottle to search for Wi-Fi.');
    }
    if (busy || wifiTimeBusy) {
      return _reject(
        'Wait for the current bottle operation or internet time sync, then search again.',
      );
    }
    final generation = _generation;
    _resetWifiScan();
    scanBusy = true;
    bannerError = null;
    notice = null;
    _notify();
    try {
      // Keeps the command queue/poll watchdog occupied until ACK + result.
      await sendCommand('wifi.scan');
    } catch (error) {
      if (!_disposed && generation == _generation) {
        scanError = error is BottleCommandException
            ? safeText(error.message)
            : 'Wi-Fi search failed. Retry near your 2.4 GHz router.';
      }
      rethrow;
    } finally {
      if (generation == _generation) scanBusy = false;
      _notify();
    }
  }

  Future<void> addWifi(String ssid, String password) async {
    final error = BottleProtocol.validateWifi(ssid, password);
    if (error != null) return _reject(error);
    if ((snapshot?.networks.length ?? 0) >= 4 &&
        !snapshot!.networks.any((n) => n['ssid'] == ssid)) {
      return _reject(
        'Four networks are already saved. Remove one before adding another.',
      );
    }
    await _mutate('wifi.add', {'ssid': ssid, 'password': password});
  }

  Future<void> removeWifi(String ssid) =>
      _mutate('wifi.remove', {'ssid': ssid});
  Future<void> syncWifi() => _mutate('wifi.sync');
  Future<void> clearPhone() => _mutate('phone.clear');
  Future<void> tare() => _mutate('tare');
  Future<void> calibrate(double grams) async {
    if (!grams.isFinite || grams <= 0) {
      return _reject('Enter a positive known reference weight in grams.');
    }
    await _mutate('calibrate', {'grams': grams});
  }

  Future<void> _reject(String message) async {
    reportError(message);
    throw BottleCommandException(message);
  }

  Future<void> _mutate(
    String command, [
    Map<String, dynamic> fields = const {},
  ]) async {
    notice = null;
    bannerError = null;
    await sendCommand(command, fields);
    // ACK means handled, not NTP success. Read actual values before announcing.
    await refreshStatus();
    notice = command.startsWith('wifi.')
        ? 'Request accepted. Internet time is confirmed only when Wi-Fi shows synced.'
        : 'Bottle acknowledged. Showing current device values.';
    _notify();
  }

  Future<void> refreshNvs() async {
    nvsLoading = true;
    nvsComplete = false;
    nvsError = null;
    nvsEntries.clear();
    _notify();
    try {
      await sendCommand('nvs');
      nvsComplete = true;
    } catch (_) {
      nvsError = 'Incomplete diagnostic read. Retry to see all keys.';
      rethrow;
    } finally {
      nvsLoading = false;
      _notify();
    }
  }

  void _onMessage(Map<String, dynamic> message) {
    if (_disposed || (!isConnected && !connecting)) return;
    final type = message['type'];
    final request = _request;
    if (type == 'ack') {
      if (request == null || message['id'] != request.id) return;
      final ok = message['ok'] == true;
      _appendLog('← #${request.id} ${ok ? 'ACK' : 'rejected'}');
      if (!ok) {
        request.fail(
          request.command == 'wifi.scan'
              ? 'Wi-Fi search could not start: ${safeText(message['error']?.toString() ?? 'bottle busy')}. Wait for internet time sync to finish, then retry.'
              : 'Bottle rejected ${request.command}: ${safeText(message['error']?.toString() ?? 'request failed')}',
        );
      } else {
        request.acknowledged = true;
        request.maybeComplete();
      }
    } else if (type == 'wifi.scan') {
      if (request == null ||
          request.command != 'wifi.scan' ||
          message['id'] != request.id ||
          request.wifiScanReceived ||
          request.done.isCompleted) {
        return;
      }
      request.wifiScanReceived = true;
      if (message['ok'] == false) {
        request.wifiScanError =
            'Wi-Fi search failed: ${safeText(message['error']?.toString() ?? 'scan unavailable')}. Retry with the bottle near your 2.4 GHz router.';
      } else {
        try {
          if (message['ok'] != true) {
            throw const FormatException('Missing search outcome');
          }
          request.wifiScan = WifiScanResult.fromMessage(message);
        } on FormatException {
          request.wifiScanError = 'Wi-Fi search returned an incomplete result. Retry or reconnect to the bottle.';
        }
      }
      request.maybeComplete();
    } else if (type == 'status') {
      if (message['protocol'] is! num || message['settings'] is! Map) {
        reportError(
          'Incomplete status notification. Check protocol v2 firmware.',
        );
        return;
      }
      final previousWifi = snapshot?.text('wifi_state');
      final previousStorageFault = snapshot?.data['storage_fault'] == true;
      snapshot = BottleStatus(
        Map<String, dynamic>.from(BottleProtocol.sanitized(message) as Map),
      );
      lastStatusAt = DateTime.now();
      if (message['ready'] == false ||
          (message['storage_fault'] == true && !previousStorageFault)) {
        reportError(
          'Bottle storage could not save data. Restart the bottle; if this continues, stop using it and service the base.',
        );
      }
      if (snapshot!.text('wifi_state') == 'failed' &&
          previousWifi != 'failed') {
        final detail = safeText(
          snapshot!.text(
            'wifi_error',
            'Check network credentials and internet access.',
          ),
        );
        reportError(
          'Wi-Fi time sync failed. $detail You can retry in Setup or use the app clock fallback.',
        );
      }
      if (request?.command == 'status') {
        request!.receivedStatus = true;
        request.maybeComplete();
      }
    } else if (type == 'event') {
      const names = {
        'drink',
        'refill',
        'lifted',
        'returned',
        'reminder',
        'missing_alert',
        'paused',
        'scale_error',
      };
      final name = names.contains(message['name'])
          ? message['name']
          : 'device event';
      final ml = message['ml'] is num ? ' · ${message['ml']} ml' : '';
      events.insert(0, '${_stamp()} $name$ml');
      if (events.length > 100) events.removeLast();
      _appendLog('← $name$ml');
    } else if (type is String &&
        type.startsWith('nvs.') &&
        request?.command == 'nvs' &&
        message['id'] == request!.id) {
      if (type == 'nvs.begin') nvsEntries.clear();
      if (type == 'nvs.entry' && message['key'] is String) {
        final key = message['key'] as String;
        final value = BottleProtocol.isSecretKey(key)
            ? (message['value'] == 'empty' || message['value'] == null
                  ? 'empty'
                  : 'set')
            : message['value'];
        nvsEntries[key] = NvsEntry(key, value, message['stored'] == true);
      }
      if (type == 'nvs.end') {
        request.nvsEnded = true;
        request.maybeComplete();
        _appendLog(
          '← NVS complete (${nvsEntries.length} keys; credentials hidden)',
        );
      }
    }
    _notify();
  }

  String _stamp() => DateTime.now().toIso8601String().substring(11, 19);
  void _appendLog(String text) {
    log.insert(0, '${_stamp()} ${safeText(text)}');
    if (log.length > 250) log.removeLast();
  }

  void clearLog() {
    log.clear();
    events.clear();
    _notify();
  }

  void _notify() {
    if (!_disposed) notifyListeners();
  }

  @override
  void dispose() {
    _disposed = true;
    ++_generation;
    _poll?.cancel();
    _reconnect?.cancel();
    _request?.fail('Controller disposed.');
    _resetWifiScan();
    for (final subscription in _subscriptions) {
      unawaited(subscription.cancel());
    }
    unawaited(_transport.dispose());
    super.dispose();
  }
}
