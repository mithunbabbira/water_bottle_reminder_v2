import 'package:bottle_tester/bottle_ble_controller.dart';
import 'package:bottle_tester/main.dart';
import 'package:bottle_tester/phone_setup.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:material_ui/material_ui.dart';

import 'fakes.dart';

void main() {
  late FakeTransport transport;
  late BottleBleController c;
  setUp(() {
    transport = FakeTransport();
    c = BottleBleController(
      transport: transport,
      sessionStore: MemorySessionStore(),
      pollInterval: Duration.zero,
    );
  });
  tearDown(() => c.dispose());

  Future<void> mount(WidgetTester tester, {double scale = 1}) async {
    tester.view.physicalSize = const Size(360, 844);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.resetPhysicalSize);
    addTearDown(tester.view.resetDevicePixelRatio);
    await c.connect(testBottle);
    await tester.pumpWidget(
      MaterialApp(
        builder: (context, child) => MediaQuery(
          data: MediaQuery.of(context)
              .copyWith(textScaler: TextScaler.linear(scale)),
          child: child!,
        ),
        home: Scaffold(
          body: SingleChildScrollView(
            child: Padding(
              padding: const EdgeInsets.all(22),
              child: AnimatedBuilder(
                animation: c,
                builder: (_, _) => PhoneSetup(controller: c),
              ),
            ),
          ),
        ),
      ),
    );
    await tester.pumpAndSettle();
  }

  Future<void> tap(WidgetTester tester, Finder finder) async {
    await tester.ensureVisible(finder);
    await tester.tap(finder);
    await tester.pumpAndSettle();
  }

  testWidgets(
    'search is discovery only; exact identity and replacement require explicit confirmation',
    (tester) async {
      await mount(tester);
      await tap(tester, find.byKey(const Key('phone-search')));
      expect(find.text('Galaxy 水'), findsOneWidget);
      expect(find.text('Unnamed Classic device'), findsOneWidget);
      expect(find.byType(TextFormField), findsNothing);
      expect(
        transport.commands.where((cmd) => cmd['cmd'] == 'phone.select'),
        isEmpty,
      );
      await tap(
        tester,
        find.byKey(const ValueKey('phone-result-22:33:44:55:66:77')),
      );
      expect(find.text('Replace selected Classic phone?'), findsOneWidget);
      expect(
        find.textContaining('22:33:44:55:66:77) as the Classic'),
        findsOneWidget,
      );
      expect(find.textContaining('clears its old presence'), findsOneWidget);
      await tester.tap(find.text('Cancel'));
      await tester.pumpAndSettle();
      expect(
        transport.commands.where((cmd) => cmd['cmd'] == 'phone.select'),
        isEmpty,
      );
      await tap(
        tester,
        find.byKey(const ValueKey('phone-result-22:33:44:55:66:77')),
      );
      await tester.tap(find.text('Replace Classic phone'));
      await tester.pumpAndSettle();
      expect(
        transport.commands.where((cmd) => cmd['cmd'] == 'phone.select').length,
        1,
      );
      expect(find.textContaining('Selected phone: Checking'), findsOneWidget);
    },
  );

  testWidgets('Classic clear confirmation keeps BLE diagnostic identity', (
    tester,
  ) async {
    await mount(tester);
    await tap(tester, find.text('Clear selected Classic phone'));
    expect(
      find.textContaining(
        'Diagnostic BLE peer, calibration and networks are kept',
      ),
      findsOneWidget,
    );
    await tester.tap(find.text('Clear Classic phone'));
    await tester.pumpAndSettle();
    expect(find.textContaining('Not configured'), findsOneWidget);
    expect(c.snapshot!.text('app_peer_mac'), '11:22:33:44:55:66');
  });

  testWidgets(
    'phone discovery errors have actionable inline text and persistent app banner',
    (tester) async {
      await c.connect(testBottle);
      await tester.pumpWidget(BottleTesterApp(controller: c));
      await tester.tap(find.text('Setup').last);
      await tester.pumpAndSettle();
      transport.onCommand = (cmd) =>
          transport.respond(cmd, ok: false, error: 'radio busy');
      await tap(tester, find.byKey(const Key('phone-search')));
      expect(c.bannerError, contains('radio busy'));
      expect(find.textContaining('radio busy'), findsWidgets);
      expect(find.byTooltip('Dismiss error'), findsOneWidget);
    },
  );

  testWidgets(
    'Classic results and confirmation wrap at narrow width and large text',
    (tester) async {
      await mount(tester, scale: 1.8);
      await tap(tester, find.byKey(const Key('phone-search')));
      expect(tester.takeException(), isNull);
      await tap(
        tester,
        find.byKey(const ValueKey('phone-result-AA:BB:CC:DD:EE:FF')),
      );
      expect(tester.takeException(), isNull);
      await tester.tap(find.text('Cancel'));
      await tester.pumpAndSettle();
      expect(tester.takeException(), isNull);
      expect(find.byType(SwitchListTile), findsNothing);
    },
  );
}
