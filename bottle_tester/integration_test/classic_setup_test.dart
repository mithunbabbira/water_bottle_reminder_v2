// Opt-in hardware setup test. Explicitly saves ONLY the exact requested device
// after fresh native Classic discovery and the bottle's live verification.
import 'package:bottle_tester/bottle_ble_controller.dart';
import 'package:bottle_tester/bottle_transport.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:integration_test/integration_test.dart';

class _NoStore implements SessionStore {
  @override
  Future<ScannedBottle?> load() async => null;
  @override
  Future<void> save(ScannedBottle? bottle) async {}
}

void main() {
  IntegrationTestWidgetsFlutterBinding.ensureInitialized();
  const enabled = bool.fromEnvironment('HARDWARE_TEST');
  const target = String.fromEnvironment('CLASSIC_PHONE_ADDRESS');
  const bottle = String.fromEnvironment('BOTTLE_DEVICE_ID');
  testWidgets(
    'native Classic picker → bottle live verify → saved target',
    (tester) async {
      await tester.runAsync(() async {
        final c = BottleBleController(sessionStore: _NoStore());
        try {
          expect(
            target,
            isNotEmpty,
            reason: 'Require explicit target, never select a random phone.',
          );
          await c.startScan();
          final devices = c.devices
              .where((d) => bottle.isEmpty || d.id == bottle)
              .toList();
          expect(devices.length, 1);
          await c.connect(devices.single);
          expect(c.isConnected, isTrue, reason: c.bannerError);
          for (var i = 0; c.wifiTimeBusy && i < 60; i++) {
            await Future<void>.delayed(const Duration(seconds: 1));
            await c.refreshStatus();
          }
          await c.scanPhones();
          expect(c.phoneDiscoverySource, 'mac_classic');
          final found = c.phones.where((p) => p.address == target).toList();
          expect(
            found.length,
            1,
            reason: 'Exact user-approved phone must be freshly discovered.',
          );
          await c.selectPhone(found.single.address, c.phoneScanId!);
          expect(c.snapshot!.text('phone_mac'), target);
          expect(c.snapshot!.text('presence_method'), 'classic_remote_name');
          await c.refreshNvs();
          expect(c.nvsEntries['classic_mac']!.stored, isTrue);
          expect(c.nvsEntries['classic_name']!.stored, isTrue);
        } finally {
          await c.disconnect();
          c.dispose();
        }
      });
    },
    skip: !enabled,
    timeout: const Timeout(Duration(minutes: 3)),
  );
}
