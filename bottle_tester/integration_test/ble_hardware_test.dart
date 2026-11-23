// OPT IN ONLY. This test deliberately connects to a real bottle, which captures
// the connecting peer. It never tares/calibrates, adds Wi-Fi, or clears identity.
//
// DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer ~/flutter/bin/flutter \
//   test integration_test/ble_hardware_test.dart -d macos \
//   --dart-define=HARDWARE_TEST=true
// Optional: --dart-define=BOTTLE_DEVICE_ID=<exact platform ID>
// Optional reversible mutation: --dart-define=MUTATE_DEVICE=true
// Classic selection is OFF by default; discovery NEVER picks an identity.
// Intentional persistent replacement requires BOTH:
// --dart-define=SELECT_CLASSIC_PHONE=true
// --dart-define=CLASSIC_PHONE_ADDRESS=AA:BB:CC:DD:EE:FF

import 'package:bottle_tester/bottle_ble_controller.dart';
import 'package:bottle_tester/bottle_protocol.dart';
import 'package:bottle_tester/bottle_transport.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:integration_test/integration_test.dart';

class _NoSavedSession implements SessionStore {
  @override
  Future<ScannedBottle?> load() async => null;
  @override
  Future<void> save(ScannedBottle? bottle) async {}
}

void main() {
  IntegrationTestWidgetsFlutterBinding.ensureInitialized();
  const enabled = bool.fromEnvironment('HARDWARE_TEST');
  const mutate = bool.fromEnvironment('MUTATE_DEVICE');
  const requestedId = String.fromEnvironment('BOTTLE_DEVICE_ID');
  const selectClassic = bool.fromEnvironment('SELECT_CLASSIC_PHONE');
  const classicAddress = String.fromEnvironment('CLASSIC_PHONE_ADDRESS');

  testWidgets(
    'real app transport ↔ bottle protocol v2',
    (tester) async {
      await tester.runAsync(() async {
        final controller = BottleBleController(
          transport: FlutterBottleTransport(),
          sessionStore: _NoSavedSession(),
          ackTimeout: const Duration(seconds: 15),
          nvsTimeout: const Duration(seconds: 30),
        );
        bool? originalDnd;
        var restoreDnd = false;
        try {
          await controller.startScan();
          expect(
            controller.bannerError,
            isNull,
            reason: 'Enable Bluetooth and grant the app Bluetooth permission.',
          );
          final candidates = requestedId.isEmpty
              ? controller.devices
              : controller.devices.where((d) => d.id == requestedId).toList();
          expect(
            candidates.length,
            1,
            reason:
                'Require exactly one intended bottle. If multiple are nearby, '
                'set BOTTLE_DEVICE_ID to avoid claiming a random peer.',
          );
          await controller.connect(candidates.single);
          expect(
            controller.isConnected,
            isTrue,
            reason: controller.bannerError,
          );
          await controller.refreshStatus();
          final status = controller.snapshot!;
          expect(status.number('protocol'), 2);
          expect(status.data['settings'], isA<Map>());
          expect(status.data.containsKey('scale_state'), isTrue);

          await controller.refreshNvs();
          expect(controller.nvsComplete, isTrue, reason: controller.nvsError);
          expect(controller.nvsEntries, isNotEmpty);
          expect(controller.nvsEntries.containsKey('schema'), isTrue);
          // Inspect public values only. Never print wire frames or credentials.
          for (final entry in controller.nvsEntries.values) {
            if (BottleProtocol.isSecretKey(entry.key)) {
              expect(entry.value, anyOf('set', 'empty', null));
              expect(entry.displayValue, anyOf('set', 'empty'));
            }
            if (!entry.stored &&
                entry.value != null &&
                !NvsEntry.requiredKeys.contains(entry.key) &&
                !NvsEntry.optionalKeys.contains(entry.key) &&
                !entry.key.startsWith('wifi_')) {
              expect(entry.classification, 'Firmware default');
            }
          }
          expect(controller.log.join('\n'), isNot(contains('"password":')));
          expect(controller.log.join('\n'), isNot(contains('"wifi_p0":')));

          // Discovery only: allow an existing boot/internet sync to finish.
          for (
            var attempt = 0;
            controller.wifiTimeBusy && attempt < 30;
            attempt++
          ) {
            await Future<void>.delayed(const Duration(seconds: 1));
            await controller.refreshStatus();
          }
          expect(
            controller.wifiTimeBusy,
            isFalse,
            reason:
                'Wait for the bottle’s current internet sync before scanning.',
          );
          final savedNetworks = controller.snapshot!.networks;
          final wifiStorage = {
            for (final entry in controller.nvsEntries.values)
              if (entry.key.startsWith('wifi_') || entry.key == 'net_count')
                entry.key: (entry.value, entry.stored),
          };
          await controller.scanWifi();
          expect(controller.scanBusy, isFalse);
          expect(controller.scanComplete, isTrue, reason: controller.scanError);
          expect(controller.scanError, isNull);
          expect(controller.wifiNetworks.length, lessThanOrEqualTo(15));
          expect(
            controller.wifiNetworks.map((n) => n.ssid).toSet().length,
            controller.wifiNetworks.length,
          );
          var previousRssi = 0;
          for (final network in controller.wifiNetworks) {
            expect(BottleProtocol.validateWifi(network.ssid, ''), isNull);
            expect(network.rssi, inInclusiveRange(-127, previousRssi));
            previousRssi = network.rssi;
          }
          // No nearby visible networks is a valid completed scan, not a failure.
          await controller.refreshStatus();
          expect(controller.snapshot!.networks, savedNetworks);
          await controller.refreshNvs();
          expect(
            {
              for (final entry in controller.nvsEntries.values)
                if (entry.key.startsWith('wifi_') || entry.key == 'net_count')
                  entry.key: (entry.value, entry.stored),
            },
            wifiStorage,
            reason: 'Searching must not modify saved credentials.',
          );

          // Read-only Classic discovery: NEVER select the first/random result.
          final originalClassic = controller.snapshot!.data['phone_mac'];
          await controller.scanPhones();
          expect(controller.phoneScanId, isNotNull);
          expect(controller.phones.length, lessThanOrEqualTo(8));
          await controller.refreshStatus();
          expect(controller.snapshot!.data['phone_mac'], originalClassic);
          if (selectClassic) {
            expect(
              isClassicAddress(classicAddress),
              isTrue,
              reason: 'Explicit selection also requires exact CLASSIC_PHONE_ADDRESS.',
            );
            expect(
              controller.phones
                  .where((p) => p.address == classicAddress)
                  .length,
              1,
              reason: 'The exact opted-in Classic identity must be in this completed discovery. Never fall back to another phone.',
            );
            await controller.selectPhone(
              classicAddress,
              controller.phoneScanId!,
            );
            expect(controller.snapshot!.text('phone_mac'), classicAddress);
          }
          if (isClassicAddress(controller.snapshot!.data['phone_mac'])) {
            // The automatic post-selection probe may still own the radio.
            for (
              var attempt = 0;
              controller.snapshot!.text('presence_state') == 'checking' &&
                  attempt < 30;
              attempt++
            ) {
              await Future<void>.delayed(const Duration(seconds: 1));
              await controller.refreshStatus();
            }
            await controller.probePhone();
            expect(
              controller.presenceState,
              anyOf('present', 'away', 'timeout', 'radio_error'),
            );
          }

          // Unknown command must be rejected by a correlated negative ACK;
          // a transport timeout is NOT a passing rejection.
          await expectLater(
            controller.sendCommand('integration.invalid_command'),
            throwsA(
              isA<BottleCommandException>().having(
                (error) => error.message,
                'negative ACK',
                contains('Bottle rejected'),
              ),
            ),
          );
          controller.clearBanner();
          await controller.refreshStatus();
          expect(controller.isConnected, isTrue);

          if (mutate) {
            originalDnd = controller.snapshot!.settings['dnd'] as bool;
            // Restore even if the write ACK or subsequent readback is lost.
            restoreDnd = true;
            await controller.saveSettings({'dnd': !originalDnd});
            expect(controller.snapshot!.settings['dnd'], !originalDnd);
          }
        } finally {
          try {
            if (restoreDnd && originalDnd != null) {
              if (!controller.isConnected) {
                fail(
                  'Link lost after optional DND mutation. Reconnect and restore '
                  'DND to $originalDnd manually before using the bottle.',
                );
              }
              await controller.saveSettings({'dnd': originalDnd});
              expect(controller.snapshot!.settings['dnd'], originalDnd);
            }
          } finally {
            await controller.disconnect();
            expect(controller.isConnected, isFalse);
            controller.dispose();
          }
        }
      });
    },
    skip: !enabled,
    timeout: const Timeout(Duration(minutes: 3)),
  );
}
