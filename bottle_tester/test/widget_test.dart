import 'package:bottle_tester/bottle_ble_controller.dart';
import 'package:bottle_tester/main.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:material_ui/material_ui.dart';

import 'fakes.dart';

void main() {
  late FakeTransport transport;
  late BottleBleController controller;
  setUp(() {
    transport = FakeTransport();
    controller = BottleBleController(
      transport: transport,
      sessionStore: MemorySessionStore(),
      pollInterval: Duration.zero,
    );
  });
  tearDown(() {
    controller.dispose();
  });

  Future<void> mount(
    WidgetTester tester, {
    double width = 390,
    bool connected = true,
  }) async {
    tester.view.physicalSize = Size(width, 844);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.resetPhysicalSize);
    addTearDown(tester.view.resetDevicePixelRatio);
    if (connected) await controller.connect(testBottle);
    await tester.pumpWidget(BottleTesterApp(controller: controller));
    await tester.pumpAndSettle();
  }

  Future<void> navigate(WidgetTester tester, String label) async {
    final navigation = find.byType(NavigationBar).evaluate().isNotEmpty
        ? find.byType(NavigationBar)
        : find.byType(NavigationRail);
    await tester.tap(
      find.descendant(of: navigation, matching: find.text(label)).last,
    );
    await tester.pumpAndSettle();
  }

  Future<void> mountWifi(
    WidgetTester tester, {
    double width = 390,
    double textScale = 1,
    bool connected = true,
  }) async {
    tester.view.physicalSize = Size(width, 844);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.resetPhysicalSize);
    addTearDown(tester.view.resetDevicePixelRatio);
    if (connected) await controller.connect(testBottle);
    await tester.pumpWidget(
      MaterialApp(
        builder: (context, child) => MediaQuery(
          data: MediaQuery.of(context)
              .copyWith(textScaler: TextScaler.linear(textScale)),
          child: child!,
        ),
        home: Scaffold(
          body: SingleChildScrollView(
            child: Padding(
              padding: const EdgeInsets.all(22),
              child: AnimatedBuilder(
                animation: controller,
                builder: (_, _) => WifiSetup(controller: controller),
              ),
            ),
          ),
        ),
      ),
    );
    await tester.pumpAndSettle();
  }

  Future<void> tapVisible(WidgetTester tester, Finder finder) async {
    await tester.pumpAndSettle();
    await tester.ensureVisible(finder);
    await tester.tap(finder);
    await tester.pumpAndSettle();
  }

  testWidgets(
    'BottleTesterApp loads offline without assuming device settings',
    (tester) async {
      await mount(tester, connected: false);
      expect(find.text('bottle'), findsOneWidget);
      expect(find.text('Meet your bottle'), findsOneWidget);
      expect(find.text('Find my bottle'), findsOneWidget);
      expect(find.text('Waiting for app connection'), findsOneWidget);
      expect(find.textContaining('Selected phone: Unknown'), findsOneWidget);
      expect(find.textContaining('Presence is always on'), findsOneWidget);
      expect(find.text('App BLE connected · present'), findsNothing);
      await navigate(tester, 'Settings');
      expect(find.text('Waiting for your bottle'), findsOneWidget);
      expect(tester.takeException(), isNull);
    },
  );
  testWidgets('Setup explains mandatory presence without a toggle', (
    tester,
  ) async {
    await mount(tester);
    await navigate(tester, 'Setup');
    expect(find.text('4  Select your Classic phone'), findsOneWidget);
    expect(
      find.textContaining('Presence is always on (read-only)'),
      findsOneWidget,
    );
    expect(
      find.textContaining('app does not need to stay open'),
      findsOneWidget,
    );
    expect(find.textContaining('system Bluetooth settings'), findsOneWidget);
    expect(
      find.textContaining('iPhone hardware testing is pending'),
      findsOneWidget,
    );
    expect(find.textContaining('Selected phone: Present'), findsOneWidget);
    expect(find.byType(SwitchListTile), findsNothing);
    expect(find.text('Require active app link for alerts'), findsNothing);
    expect(find.textContaining('Optional: reminders'), findsNothing);
    await navigate(tester, 'Settings');
    expect(find.text('Mute sound (Do not disturb)'), findsOneWidget);
    expect(
      find.byKey(const ValueKey('setting-presence_enabled')),
      findsNothing,
    );
    expect(transport.commands.where((c) => c['cmd'] == 'settings'), isEmpty);
  });

  for (final scenario in [
    (true, true, 'Present · selected phone replied over Classic'),
    (false, true, 'Present · selected phone replied over Classic'),
    (false, false, 'Unknown · waiting for a fresh Classic check'),
    (true, false, 'Unknown · waiting for a fresh Classic check'),
    (null, null, 'Unknown · waiting for a fresh Classic check'),
  ]) {
    testWidgets('Home displays actual presence: ${scenario.$3}', (
      tester,
    ) async {
      transport.status['phone_connected'] = scenario.$1;
      transport.status['phone_present'] = scenario.$2;
      await mount(tester);
      expect(find.textContaining(scenario.$3), findsOneWidget);
      expect(find.textContaining('Presence is always on'), findsOneWidget);
      expect(find.textContaining('Presence gate off'), findsNothing);
      // Next check keeps the countdown even when Classic presence is stale;
      // presence detail stays in At a glance / Setup.
      expect(find.text('In 15 min'), findsOneWidget);
      expect(
        find.text('Alerts wait for a fresh selected-phone Classic check'),
        findsNothing,
      );
      expect(tester.takeException(), isNull);
    });
  }

  for (final width in [390.0, 1280.0]) {
    testWidgets('all pages render without overflow at ${width.toInt()}px', (
      tester,
    ) async {
      await mount(tester, width: width);
      expect(find.text('250 ml'), findsOneWidget);
      expect(find.text('650.5 g'), findsOneWidget);
      expect(tester.takeException(), isNull);
      for (final page in ['Setup', 'Settings', 'Advanced', 'Home']) {
        await navigate(tester, page);
        expect(tester.takeException(), isNull, reason: '$page at $width');
        await tester.drag(
          find.byType(SingleChildScrollView).first,
          const Offset(0, -500),
        );
        await tester.pumpAndSettle();
        expect(
          tester.takeException(),
          isNull,
          reason: '$page scrolled at $width',
        );
      }
    });
  }
  testWidgets('large text narrow layout also wraps', (tester) async {
    await controller.connect(testBottle);
    tester.view.physicalSize = const Size(390, 844);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.resetPhysicalSize);
    addTearDown(tester.view.resetDevicePixelRatio);
    await tester.pumpWidget(
      MediaQuery(
        data: const MediaQueryData(textScaler: TextScaler.linear(1.4)),
        child: BottleTesterApp(controller: controller),
      ),
    );
    await tester.pumpAndSettle();
    await navigate(tester, 'Setup');
    expect(tester.takeException(), isNull);
  });
  testWidgets(
    'settings populate actual status, not factory defaults; save needs ACK',
    (tester) async {
      await mount(tester);
      await navigate(tester, 'Settings');
      TextFormField field(String key) =>
          tester.widget<TextFormField>(find.byKey(ValueKey('setting-$key')));
      expect(field('start_min').controller!.text, '07:00');
      expect(field('end_min').controller!.text, '21:00');
      expect(field('interval_min').controller!.text, '45');
      expect(field('missing_color').controller!.text, 'FFB400');
      expect(find.text('Mute sound (Do not disturb)'), findsOneWidget);
      expect(
        find.text('Keeps reminder lights on, but silences the buzzer'),
        findsOneWidget,
      );
      await tester.enterText(
        find.byKey(const ValueKey('setting-interval_min')),
        '20',
      );
      await tester.pump();
      expect(find.textContaining('Unsaved changes'), findsOneWidget);
      expect(controller.snapshot!.settings['interval_min'], 45);
      final button = find.widgetWithText(FilledButton, 'Save settings');
      await tester.ensureVisible(button);
      await tester.tap(button);
      await tester.pumpAndSettle();
      expect(controller.snapshot!.settings['interval_min'], 20);
      expect(
        find.text('Device values loaded · edit, then save'),
        findsOneWidget,
      );
      expect(tester.takeException(), isNull);
    },
  );
  testWidgets(
    'settings show pending then visible ACK failure without optimistic save',
    (tester) async {
      await mount(tester);
      await navigate(tester, 'Settings');
      transport.autoRespond = false;
      await tester.enterText(
        find.byKey(const ValueKey('setting-interval_min')),
        '20',
      );
      await tester.pump();
      final save = find.widgetWithText(FilledButton, 'Save settings');
      await tester.ensureVisible(save);
      await tester.tap(save);
      await tester.pump();
      expect(find.text('Saving…'), findsOneWidget);
      expect(controller.snapshot!.settings['interval_min'], 45);
      final request = transport.commands.last;
      transport.emit({
        'type': 'ack',
        'id': request['id'],
        'ok': false,
        'error': 'storage_failed',
      });
      await tester.pumpAndSettle();
      expect(find.textContaining('Bottle rejected settings'), findsOneWidget);
      expect(find.textContaining('Unsaved changes'), findsOneWidget);
      expect(controller.snapshot!.settings['interval_min'], 45);
    },
  );
  testWidgets('invalid reminder settings stay unsaved with validation', (
    tester,
  ) async {
    await mount(tester);
    await navigate(tester, 'Settings');
    await tester.enterText(
      find.byKey(const ValueKey('setting-start_min')),
      '25:00',
    );
    await tester.pump();
    final save = find.widgetWithText(FilledButton, 'Save settings');
    await tester.ensureVisible(save);
    await tester.tap(save);
    await tester.pumpAndSettle();
    expect(find.text('Use HH:mm (00:00–23:59).'), findsOneWidget);
    expect(transport.commands.where((c) => c['cmd'] == 'settings'), isEmpty);
  });
  testWidgets(
    'Wi-Fi form preserves spaces, rejects short password, then reads device networks',
    (tester) async {
      await mountWifi(tester);
      expect(find.byKey(const Key('wifi-ssid')), findsNothing);
      await tapVisible(tester, find.byKey(const Key('wifi-manual-toggle')));
      await tester.enterText(
        find.byKey(const Key('wifi-ssid')),
        ' Living room ',
      );
      await tester.pump();
      await tester.enterText(find.byKey(const Key('wifi-password')), 'short');
      await tapVisible(
        tester,
        find.widgetWithText(FilledButton, 'Save and sync'),
      );
      expect(find.textContaining('Password must be empty'), findsOneWidget);
      expect(transport.commands.where((c) => c['cmd'] == 'wifi.add'), isEmpty);
      await tester.enterText(
        find.byKey(const Key('wifi-password')),
        'my good password',
      );
      await tapVisible(
        tester,
        find.widgetWithText(FilledButton, 'Save and sync'),
      );
      expect(find.text(' Living room '), findsNWidgets(2));
      expect(controller.snapshot!.networks.single['ssid'], ' Living room ');
      final password = tester.widget<TextFormField>(
        find.byKey(const Key('wifi-password')),
      );
      expect(password.controller!.text, isEmpty);
      expect(controller.log.join(), isNot(contains('my good password')));
      transport.status['wifi_state'] = 'synced';
      transport.emit(transport.status);
      await tester.pumpAndSettle();
      await tester.ensureVisible(find.byTooltip('Remove network'));
      await tester.tap(find.byTooltip('Remove network'));
      await tester.pumpAndSettle();
      expect(find.text('Remove network?'), findsOneWidget);
      await tester.tap(find.widgetWithText(FilledButton, 'Remove'));
      await tester.pumpAndSettle();
      expect(controller.snapshot!.networks, isEmpty);
    },
  );
  testWidgets(
    'search is primary; selection preserves exact SSID and only explicit save mutates',
    (tester) async {
      await mountWifi(tester, width: 320);
      expect(find.text('Search networks'), findsOneWidget);
      expect(find.byKey(const Key('wifi-ssid')), findsNothing);
      expect(
        tester
            .widget<FilledButton>(find.byKey(const Key('wifi-search')))
            .onPressed,
        isNotNull,
      );
      final save = find.widgetWithText(FilledButton, 'Save and sync');
      expect(tester.widget<FilledButton>(save).onPressed, isNull);
      await tapVisible(tester, find.byKey(const Key('wifi-search')));
      expect(find.textContaining('Strong signal · -43 dBm'), findsOneWidget);
      expect(find.textContaining('Password required'), findsOneWidget);
      expect(find.textContaining('Open network'), findsOneWidget);
      final secured = find.byKey(
        const ValueKey('wifi-result- Living room Café 水 '),
      );
      final open = find.byKey(const ValueKey('wifi-result-Guest'));
      expect(
        tester.getTopLeft(secured).dy,
        lessThan(tester.getTopLeft(open).dy),
      );
      await tapVisible(tester, secured);
      expect(find.text(' Living room Café 水 '), findsNWidgets(2));
      await tapVisible(tester, save);
      expect(find.text('Enter this network’s Wi-Fi password.'), findsOneWidget);
      expect(transport.commands.any((c) => c['cmd'] == 'wifi.add'), isFalse);
      await tester.ensureVisible(find.byKey(const Key('wifi-password')));
      await tester.enterText(
        find.byKey(const Key('wifi-password')),
        'first password',
      );
      // Re-selecting the same network keeps its draft password.
      await tapVisible(tester, secured);
      TextFormField password() =>
          tester.widget<TextFormField>(find.byKey(const Key('wifi-password')));
      expect(password().controller!.text, 'first password');
      await tapVisible(tester, open);
      expect(password().controller!.text, isEmpty);
      expect(transport.commands.map((c) => c['cmd']), ['status', 'wifi.scan']);
      expect(controller.snapshot!.networks, isEmpty);
      await tapVisible(tester, secured);
      await tester.ensureVisible(find.byKey(const Key('wifi-password')));
      await tester.enterText(
        find.byKey(const Key('wifi-password')),
        'exact password',
      );
      await tapVisible(tester, save);
      final command = transport.commands.firstWhere(
        (c) => c['cmd'] == 'wifi.add',
      );
      expect(command['ssid'], ' Living room Café 水 ');
      expect(command['password'], 'exact password');
      expect(password().controller!.text, isEmpty);
      expect(controller.log.join(), isNot(contains('exact password')));
      expect(tester.takeException(), isNull);
    },
  );

  testWidgets(
    'manual option is hidden by default, preserves spaces and clears password on name edits',
    (tester) async {
      await mountWifi(tester);
      expect(find.byKey(const Key('wifi-ssid')), findsNothing);
      await tapVisible(tester, find.byKey(const Key('wifi-manual-toggle')));
      await tester.enterText(find.byKey(const Key('wifi-ssid')), ' Hidden 水 ');
      await tester.enterText(
        find.byKey(const Key('wifi-password')),
        'old password',
      );
      await tester.enterText(find.byKey(const Key('wifi-ssid')), ' Other 水 ');
      expect(
        tester
            .widget<TextFormField>(find.byKey(const Key('wifi-password')))
            .controller!
            .text,
        isEmpty,
      );
      await tapVisible(tester, find.byKey(const Key('wifi-manual-toggle')));
      expect(find.byKey(const Key('wifi-ssid')), findsNothing);
      expect(find.text(' Other 水 '), findsOneWidget);
      expect(transport.commands.map((c) => c['cmd']), ['status']);
      expect(tester.takeException(), isNull);
    },
  );

  testWidgets(
    'scan status waits for both messages, failures offer retry and 2.4 GHz location help',
    (tester) async {
      await mountWifi(tester, width: 320);
      transport.autoRespond = false;
      final button = find.byKey(const Key('wifi-search'));
      await tester.ensureVisible(button);
      await tester.tap(button);
      await tester.pump();
      expect(find.text('Searching…'), findsOneWidget);
      expect(tester.widget<FilledButton>(button).onPressed, isNull);
      final id = transport.commands.last['id'];
      transport.emit({
        'type': 'wifi.scan',
        'id': id + 1,
        'ok': true,
        'networks': transport.scanNetworks,
      });
      transport.emit({
        'type': 'wifi.scan',
        'id': id,
        'ok': true,
        'networks': transport.scanNetworks,
      });
      await tester.pump();
      expect(find.byKey(const ValueKey('wifi-result-Guest')), findsNothing);
      transport.emit({'type': 'ack', 'id': id, 'ok': true});
      await tester.pumpAndSettle();
      expect(find.byKey(const ValueKey('wifi-result-Guest')), findsOneWidget);
      await tester.ensureVisible(button);
      await tester.tap(button);
      await tester.pump();
      expect(find.byKey(const ValueKey('wifi-result-Guest')), findsNothing);
      final failedId = transport.commands.last['id'];
      transport.emit({'type': 'ack', 'id': failedId, 'ok': true});
      transport.emit({
        'type': 'wifi.scan',
        'id': failedId,
        'ok': false,
        'error': 'scan_failed',
        'networks': <dynamic>[],
      });
      await tester.pumpAndSettle();
      expect(find.textContaining('Wi-Fi search failed'), findsOneWidget);
      expect(find.text('Search again'), findsOneWidget);
      expect(find.textContaining('Move the bottle closer'), findsOneWidget);
      expect(find.textContaining('5 GHz-only'), findsOneWidget);
      expect(find.textContaining('NVS'), findsNothing);
      transport.autoRespond = true;
      transport.scanNetworks = [];
      await tapVisible(tester, button);
      expect(find.textContaining('No visible networks found'), findsOneWidget);
      expect(
        find
            .textContaining('No visible networks found')
            .evaluate()
            .single
            .widget,
        isA<Text>(),
      );
      expect(controller.scanError, isNull);
      expect(transport.commands.where((c) => c['cmd'] == 'wifi.add'), isEmpty);
      expect(tester.takeException(), isNull);
    },
  );

  testWidgets(
    'search disabled while offline, other command busy or internet sync active',
    (tester) async {
      await mountWifi(tester, connected: false);
      final button = find.byKey(const Key('wifi-search'));
      expect(tester.widget<FilledButton>(button).onPressed, isNull);
      await controller.connect(testBottle);
      await tester.pumpAndSettle();
      for (final state in ['connecting', 'syncing']) {
        transport.emit({...transport.status, 'wifi_state': state});
        await tester.pumpAndSettle();
        expect(tester.widget<FilledButton>(button).onPressed, isNull);
        expect(
          find.textContaining('Internet time sync is running'),
          findsOneWidget,
        );
      }
      transport.emit(transport.status);
      transport.autoRespond = false;
      final other = controller.refreshStatus();
      await tester.pump();
      expect(tester.widget<FilledButton>(button).onPressed, isNull);
      transport.respond(transport.commands.last);
      await other;
      await tester.pumpAndSettle();
      expect(tester.widget<FilledButton>(button).onPressed, isNotNull);
      expect(transport.commands.where((c) => c['cmd'] == 'wifi.scan'), isEmpty);
    },
  );

  testWidgets(
    'long scan names wrap without overflow at narrow width and large text',
    (tester) async {
      await mountWifi(tester, width: 320, textScale: 1.4);
      transport.scanNetworks = [
        {
          'ssid': 'abcdefghijklmnopqrstuvwxyz123456',
          'rssi': -52,
          'secured': true,
        },
        {'ssid': 'Guest network', 'rssi': -82, 'secured': false},
      ];
      await tapVisible(tester, find.byKey(const Key('wifi-search')));
      await tapVisible(
        tester,
        find.byKey(
          const ValueKey('wifi-result-abcdefghijklmnopqrstuvwxyz123456'),
        ),
      );
      expect(tester.takeException(), isNull);
      await tapVisible(tester, find.byKey(const Key('wifi-manual-toggle')));
      expect(tester.takeException(), isNull);
    },
  );

  testWidgets('tare requires explicit empty-base confirmation', (tester) async {
    await mount(tester);
    await navigate(tester, 'Setup');
    final tare = find.widgetWithText(OutlinedButton, 'Tare empty base');
    await tester.ensureVisible(tare);
    await tester.tap(tare);
    await tester.pumpAndSettle();
    expect(find.text('Is the base completely empty?'), findsOneWidget);
    expect(transport.commands.where((c) => c['cmd'] == 'tare'), isEmpty);
    await tester.tap(find.widgetWithText(FilledButton, 'Tare empty base'));
    await tester.pumpAndSettle();
    expect(transport.commands.where((c) => c['cmd'] == 'tare').length, 1);
  });
  testWidgets('calibration requires a placed known weight and positive grams', (
    tester,
  ) async {
    await controller.connect(testBottle);
    await tester.pumpWidget(
      MaterialApp(
        home: Builder(
          builder: (context) => Scaffold(
            body: TextButton(
              onPressed: () => showDialog<void>(
                context: context,
                builder: (_) => CalibrationDialog(controller: controller),
              ),
              child: const Text('Open'),
            ),
          ),
        ),
      ),
    );
    await tester.tap(find.text('Open'));
    await tester.pumpAndSettle();
    final calibrate = find.widgetWithText(FilledButton, 'Calibrate');
    expect(tester.widget<FilledButton>(calibrate).onPressed, isNull);
    await tester.tap(find.byType(CheckboxListTile));
    await tester.pumpAndSettle();
    await tester.tap(calibrate);
    await tester.pumpAndSettle();
    expect(find.text('Enter a positive weight.'), findsOneWidget);
    await tester.enterText(find.byType(TextFormField), '500');
    await tester.tap(calibrate);
    await tester.pumpAndSettle();
    expect(
      transport.commands.firstWhere((c) => c['cmd'] == 'calibrate')['grams'],
      500,
    );
  });
  testWidgets('NVS defaults look informational, not scary missing faults', (
    tester,
  ) async {
    await mount(tester);
    await controller.refreshNvs();
    await navigate(tester, 'Advanced');
    expect(find.text('600 · Firmware default'), findsOneWidget);
    expect(find.textContaining('Scale setup needed:'), findsNothing);
    expect(find.text('MISSING'), findsNothing);
    expect(controller.bannerError, isNull);
    expect(tester.takeException(), isNull);
  });
  testWidgets('visible error banner persists outside scroll and can dismiss', (
    tester,
  ) async {
    await mount(tester);
    controller.reportError('An actionable device error');
    await tester.pumpAndSettle();
    expect(find.text('An actionable device error'), findsOneWidget);
    await tester.drag(
      find.byType(SingleChildScrollView).first,
      const Offset(0, -800),
    );
    await tester.pumpAndSettle();
    expect(
      find.text('An actionable device error').hitTestable(),
      findsOneWidget,
    );
    await tester.tap(find.byTooltip('Dismiss error'));
    await tester.pumpAndSettle();
    expect(find.text('An actionable device error'), findsNothing);
  });
  testWidgets('connecting to scan result requires peer-capture confirmation', (
    tester,
  ) async {
    await mount(tester, connected: false);
    await controller.startScan();
    await tester.pumpAndSettle();
    final button = find.byTooltip('Connect to Kitchen bottle');
    await tester.ensureVisible(button);
    await tester.tap(button);
    await tester.pumpAndSettle();
    expect(find.textContaining('configuration only'), findsOneWidget);
    expect(transport.connects, 0);
    await tester.tap(find.widgetWithText(FilledButton, 'Connect'));
    await tester.pumpAndSettle();
    expect(transport.connects, 1);
  });
}
