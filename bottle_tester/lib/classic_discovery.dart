import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';

/// Fresh Classic inquiry only. Implementations must not return paired-device
/// inventories as discoveries or interpret names as proof of presence.
abstract class ClassicDiscovery {
  Future<Map<String, dynamic>> scan(int requestId);
  Future<void> cancel();
}

ClassicDiscovery? defaultClassicDiscovery() =>
    !kIsWeb &&
        defaultTargetPlatform == TargetPlatform.macOS &&
        !const bool.fromEnvironment('ESP_CLASSIC_DISCOVERY')
    ? MethodChannelClassicDiscovery()
    : null;

class ClassicDiscoveryException implements Exception {
  const ClassicDiscoveryException(this.message);
  final String message;
  @override
  String toString() => message;
}

class MethodChannelClassicDiscovery implements ClassicDiscovery {
  MethodChannelClassicDiscovery({MethodChannel? channel})
    : _channel = channel ?? const MethodChannel('bottle/classic_discovery');
  final MethodChannel _channel;
  int? _activeId;

  @override
  Future<Map<String, dynamic>> scan(int requestId) async {
    if (_activeId != null) {
      throw const ClassicDiscoveryException(
        'Mac Classic discovery is busy. Wait or cancel the current search.',
      );
    }
    _activeId = requestId;
    try {
      final result = await _channel.invokeMapMethod<String, dynamic>('scan', {
        'id': requestId,
      });
      if (result == null ||
          result['id'] != requestId ||
          result['discovery_source'] != 'mac_classic') {
        throw const ClassicDiscoveryException(
          'Mac Classic search returned an invalid or stale result. Search again.',
        );
      }
      return result;
    } on MissingPluginException {
      throw const ClassicDiscoveryException(
        'Native Mac Classic discovery is unavailable in this build. Rebuild the macOS app. No ESP fallback was run.',
      );
    } on PlatformException catch (error) {
      final message = switch (error.code) {
        'permission' => 'Allow Bottle Bluetooth access in System Settings > Privacy & Security > Bluetooth, then retry.',
        'busy' => 'Mac Classic discovery is busy. Wait for the current inquiry to finish and retry.',
        'timeout' => 'Mac Classic search timed out after 30 seconds. Open phone Bluetooth settings and retry.',
        'cancelled' => 'Mac Classic search cancelled. Search again when ready.',
        _ => 'Mac Classic search failed. Turn Bluetooth on, check Bluetooth permission and retry. No cached or ESP fallback results were used.',
      };
      throw ClassicDiscoveryException(message);
    } finally {
      if (_activeId == requestId) _activeId = null;
    }
  }

  @override
  Future<void> cancel() async {
    final id = _activeId;
    if (id == null) return;
    try {
      await _channel.invokeMethod<void>('cancel', {'id': id});
    } on MissingPluginException {
      // Nothing native exists to cancel.
    } finally {
      if (_activeId == id) _activeId = null;
    }
  }
}
