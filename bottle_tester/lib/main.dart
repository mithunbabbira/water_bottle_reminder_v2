import 'dart:async';

import 'package:material_ui/material_ui.dart';

import 'action_confirmation.dart';
import 'bottle_ble_controller.dart';
import 'bottle_protocol.dart';
import 'settings_editor.dart';
import 'phone_setup.dart';

void main() {
  WidgetsFlutterBinding.ensureInitialized();
  runApp(const BottleTesterApp());
}

class BottleTesterApp extends StatelessWidget {
  const BottleTesterApp({super.key, this.controller});
  final BottleBleController? controller;
  @override
  Widget build(BuildContext context) => MaterialApp(
    title: 'Bottle · a little sip, a better day',
    debugShowCheckedModeBanner: false,
    theme: ThemeData(
      useMaterial3: true,
      colorScheme: ColorScheme.fromSeed(seedColor: const Color(0xff087f8c)),
      scaffoldBackgroundColor: const Color(0xfff5f8f8),
      inputDecorationTheme: const InputDecorationTheme(
        border: OutlineInputBorder(),
      ),
      cardTheme: CardThemeData(
        elevation: 0,
        color: Colors.white,
        shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(24)),
      ),
    ),
    home: BottlePage(controller: controller),
  );
}

class BottlePage extends StatefulWidget {
  const BottlePage({super.key, this.controller});
  final BottleBleController? controller;
  @override
  State<BottlePage> createState() => _BottlePageState();
}

class _BottlePageState extends State<BottlePage> with WidgetsBindingObserver {
  late final BottleBleController c;
  int _page = 0;
  static const _labels = ['Home', 'Setup', 'Settings', 'Advanced'];
  static const _icons = [
    Icons.water_drop_outlined,
    Icons.auto_awesome_outlined,
    Icons.tune,
    Icons.monitor_heart_outlined,
  ];

  @override
  void initState() {
    super.initState();
    c = widget.controller ?? BottleBleController();
    WidgetsBinding.instance.addObserver(this);
    if (widget.controller == null) unawaited(c.initialize());
  }

  @override
  void didChangeAppLifecycleState(AppLifecycleState state) {
    if (state == AppLifecycleState.resumed && c.isConnected && !c.busy) {
      unawaited(_run(c.refreshStatus));
    }
  }

  @override
  void dispose() {
    WidgetsBinding.instance.removeObserver(this);
    if (widget.controller == null) c.dispose();
    super.dispose();
  }

  Future<void> _run(Future<void> Function() action) async {
    try {
      await action();
    } catch (_) {
      /* Controller owns visible errors. */
    }
  }

  @override
  Widget build(BuildContext context) => AnimatedBuilder(
    animation: c,
    builder: (context, _) {
      final wide = MediaQuery.sizeOf(context).width >= 840;
      return Scaffold(
        appBar: AppBar(
          title: const Text(
            'bottle',
            style: TextStyle(fontWeight: FontWeight.w800, letterSpacing: -1),
          ),
          backgroundColor: const Color(0xfff5f8f8),
          actions: [
            Padding(
              padding: const EdgeInsets.only(right: 16),
              child: Center(
                child: Text(
                  c.connecting
                      ? 'Connecting…'
                      : c.isConnected
                      ? '● Connected'
                      : '○ Offline',
                  style: TextStyle(
                    color: c.isConnected ? const Color(0xff087f8c) : null,
                  ),
                ),
              ),
            ),
          ],
        ),
        body: SafeArea(
          child: Row(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              if (wide)
                NavigationRail(
                  selectedIndex: _page,
                  labelType: NavigationRailLabelType.all,
                  backgroundColor: const Color(0xfff5f8f8),
                  onDestinationSelected: (value) =>
                      setState(() => _page = value),
                  destinations: [
                    for (var i = 0; i < _labels.length; i++)
                      NavigationRailDestination(
                        icon: Icon(_icons[i]),
                        label: Text(_labels[i]),
                      ),
                  ],
                ),
              Expanded(
                child: Column(
                  children: [
                    if (c.bannerError != null)
                      _banner(c.bannerError!, error: true),
                    if (c.busy) const LinearProgressIndicator(minHeight: 3),
                    Expanded(
                      child: SingleChildScrollView(
                        key: ValueKey('page-$_page'),
                        padding: EdgeInsets.all(wide ? 28 : 16),
                        child: Center(
                          child: ConstrainedBox(
                            constraints: const BoxConstraints(maxWidth: 1060),
                            child: Column(
                              crossAxisAlignment: CrossAxisAlignment.stretch,
                              children: [
                                Text(
                                  _labels[_page],
                                  style: Theme.of(context)
                                      .textTheme
                                      .headlineLarge
                                      ?.copyWith(fontWeight: FontWeight.w700),
                                ),
                                const SizedBox(height: 6),
                                Text(
                                  [
                                    'Small sips. A healthier rhythm.',
                                    'A few simple steps, made for your day.',
                                    'Your bottle, your rhythm.',
                                    'Device details for troubleshooting.',
                                  ][_page],
                                ),
                                const SizedBox(height: 22),
                                if (c.notice != null)
                                  Padding(
                                    padding: const EdgeInsets.only(bottom: 16),
                                    child: _banner(c.notice!),
                                  ),
                                if (!c.isConnected || _page == 0)
                                  _connectionCard(),
                                if (_page == 0) ..._home(),
                                if (_page == 1) ..._setup(),
                                if (_page == 2) ...[
                                  if (c.snapshot == null)
                                    const InfoCard(
                                      title: 'Waiting for your bottle',
                                      child: Text(
                                        'Connect to load actual device settings. Nothing is assumed or saved while offline.',
                                      ),
                                    ),
                                  if (c.snapshot != null)
                                    SettingsEditor(controller: c),
                                ],
                                if (_page == 3) ..._advanced(),
                                const SizedBox(height: 28),
                              ],
                            ),
                          ),
                        ),
                      ),
                    ),
                  ],
                ),
              ),
            ],
          ),
        ),
        bottomNavigationBar: wide
            ? null
            : NavigationBar(
                selectedIndex: _page,
                onDestinationSelected: (value) => setState(() => _page = value),
                destinations: [
                  for (var i = 0; i < _labels.length; i++)
                    NavigationDestination(
                      icon: Icon(_icons[i]),
                      label: _labels[i],
                    ),
                ],
              ),
      );
    },
  );

  Widget _banner(String message, {bool error = false}) => Container(
    width: double.infinity,
    padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 10),
    decoration: BoxDecoration(
      color: error
          ? Theme.of(context).colorScheme.errorContainer
          : const Color(0xffe0f2f1),
      borderRadius: BorderRadius.circular(12),
    ),
    child: Row(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Icon(error ? Icons.error_outline : Icons.info_outline, size: 20),
        const SizedBox(width: 10),
        Expanded(child: Text(message)),
        if (error)
          IconButton(
            tooltip: 'Dismiss error',
            onPressed: c.clearBanner,
            icon: const Icon(Icons.close, size: 18),
          ),
      ],
    ),
  );

  Widget _connectionCard() => InfoCard(
    title: c.isConnected ? c.connected!.name : 'Meet your bottle',
    icon: Icons.bluetooth,
    child: Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        Text(
          c.isConnected ? 'Live updates every 2 seconds while connected.' : 'Power the bottle and stay nearby. Scanning never connects automatically to a new bottle.',
        ),
        const SizedBox(height: 14),
        Wrap(
          spacing: 10,
          runSpacing: 8,
          children: [
            if (!c.isConnected && !c.connecting)
              FilledButton.icon(
                onPressed: c.scanning
                    ? () => _run(c.stopScan)
                    : () => _run(c.startScan),
                icon: Icon(c.scanning ? Icons.stop : Icons.search),
                label: Text(c.scanning ? 'Stop scanning' : 'Find my bottle'),
              ),
            if (c.isConnected || c.connecting)
              OutlinedButton(
                onPressed: () => _run(c.disconnect),
                child: Text(c.connecting ? 'Cancel connection' : 'Disconnect'),
              ),
            if (c.isConnected)
              OutlinedButton.icon(
                onPressed: c.busy ? null : () => _run(c.refreshStatus),
                icon: const Icon(Icons.refresh),
                label: const Text('Refresh'),
              ),
          ],
        ),
        if (c.scanning)
          const Padding(
            padding: EdgeInsets.only(top: 16),
            child: LinearProgressIndicator(),
          ),
        if (!c.isConnected)
          for (final device in c.devices)
            ListTile(
              contentPadding: EdgeInsets.zero,
              title: Text(device.name),
              subtitle: Text('${device.id}\n${device.rssi} dBm'),
              isThreeLine: true,
              trailing: IconButton(
                tooltip: 'Connect to ${device.name}',
                icon: const Icon(Icons.chevron_right),
                onPressed: c.connecting
                    ? null
                    : () async {
                        final approved = await confirmAction(
                          context,
                          title: 'Connect to ${device.name}?',
                          message: 'Connect for configuration only. The app BLE link is not phone presence and never selects a Classic phone. Only connect to a bottle you intend to configure.',
                          confirmLabel: 'Connect',
                        );
                        if (approved) await _run(() => c.connect(device));
                      },
              ),
            ),
        if (c.isConnected)
          SwitchListTile.adaptive(
            contentPadding: EdgeInsets.zero,
            title: const Text('Remember this bottle'),
            subtitle: const Text(
              'Reconnect only to this exact bottle on next launch; at most one retry after a lost link.',
            ),
            value: c.rememberConnection,
            onChanged: (v) => _run(() => c.setRememberConnection(v)),
          ),
        if (!c.isConnected && c.remembered != null)
          TextButton(
            onPressed: c.connecting
                ? null
                : () => _run(() => c.connect(c.remembered!)),
            child: const Text('Retry saved bottle'),
          ),
        if (!c.isConnected && c.remembered != null)
          TextButton(
            onPressed: () => _run(() => c.setRememberConnection(false)),
            child: const Text('Forget saved bottle'),
          ),
      ],
    ),
  );

  List<Widget> _home() {
    final s = c.snapshot;
    return [
      Container(
        padding: const EdgeInsets.all(28),
        margin: const EdgeInsets.only(bottom: 16),
        decoration: BoxDecoration(
          borderRadius: BorderRadius.circular(28),
          gradient: const LinearGradient(
            colors: [Color(0xff076774), Color(0xff159e98)],
          ),
        ),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            const Icon(Icons.water_drop, color: Color(0xffb9f3ea), size: 36),
            const SizedBox(height: 18),
            const Text(
              'WATER TODAY',
              style: TextStyle(color: Colors.white, letterSpacing: 2),
            ),
            Text(
              s == null ? '—' : '${_number(s.number('today_ml'))} ml',
              style: const TextStyle(
                color: Colors.white,
                fontSize: 48,
                fontWeight: FontWeight.w700,
              ),
            ),
            const SizedBox(height: 8),
            Text(
              s == null
                  ? 'Connect to see your progress.'
                  : 'Last sip · ${_number(s.number('last_drink_ml'))} ml',
              style: const TextStyle(color: Colors.white),
            ),
            const SizedBox(height: 14),
            Text(
              c.stale
                  ? 'Updates delayed · values may be out of date'
                  : s != null
                  ? 'Live from your bottle · estimated from weight changes'
                  : 'Your bottle keeps the daily total.',
              style: const TextStyle(color: Color(0xffd4f7f0)),
            ),
          ],
        ),
      ),
      LayoutBuilder(
        builder: (context, constraints) {
          final width = constraints.maxWidth >= 620
              ? (constraints.maxWidth - 16) / 2
              : constraints.maxWidth;
          return Wrap(
            spacing: 16,
            runSpacing: 0,
            children: [
              SizedBox(
                width: width,
                child: InfoCard(
                  title: 'Bottle status',
                  icon: Icons.sensors,
                  child: Text(
                    s == null
                        ? 'Waiting for connection'
                        : _stateLabel(s.text('state')),
                  ),
                ),
              ),
              SizedBox(
                width: width,
                child: InfoCard(
                  title: 'Next check',
                  icon: Icons.schedule,
                  child: Text(_nextCheckLabel(c, s)),
                ),
              ),
            ],
          );
        },
      ),
      InfoCard(
        title: 'Live scale',
        icon: Icons.scale_outlined,
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(
              s?.number('weight_g') == null
                  ? '—'
                  : '${s!.number('weight_g')!.toStringAsFixed(1)} g',
              style: Theme.of(context).textTheme.headlineMedium,
            ),
            Text('Scale: ${_scaleLabel(s?.text('scale_state') ?? 'offline')}'),
            if (s != null && s.text('scale_state') != 'ready')
              TextButton(
                onPressed: () => setState(() => _page = 1),
                child: const Text('Open scale setup'),
              ),
          ],
        ),
      ),
      InfoCard(
        title: 'At a glance',
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            if (s != null) ...[
              Text(
                'Clock: ${s.flag('clock_valid') ? 'ready' : 'needs sync'} · ${_sourceLabel(s.text('time_source'))}',
              ),
              const SizedBox(height: 8),
              Text('Internet time: ${s.text('wifi_state')}'),
              const SizedBox(height: 8),
            ],
            const Text(
              'Presence is always on · alerts require fresh selected-phone Classic reachability, not an app connection.',
            ),
            const SizedBox(height: 8),
            Text(presenceLabel(c)),
            TextButton(
              onPressed: () => setState(() => _page = 1),
              child: const Text('Review setup'),
            ),
          ],
        ),
      ),
    ];
  }

  List<Widget> _setup() {
    final s = c.snapshot;
    return [
      InfoCard(
        title: '1  Set your local time',
        icon: Icons.schedule,
        child: TimeSetup(controller: c),
      ),
      InfoCard(
        title: '2  Make the scale yours',
        icon: Icons.scale_outlined,
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text('Scale: ${_scaleLabel(s?.text('scale_state') ?? 'offline')}'),
            const SizedBox(height: 8),
            const Text(
              'First remove the bottle and everything on the base. Tare only the empty base. Then place a known reference weight to calibrate—not an unknown bottle weight.',
            ),
            const SizedBox(height: 16),
            Wrap(
              spacing: 10,
              runSpacing: 10,
              children: [
                OutlinedButton(
                  onPressed: !c.isConnected || c.busy
                      ? null
                      : () async {
                          if (await confirmAction(
                            context,
                            title: 'Is the base completely empty?',
                            message: 'Remove the bottle and all objects. Keep the base level and still. Taring with a bottle on it will make water measurements incorrect.',
                            confirmLabel: 'Tare empty base',
                          )) {
                            await _run(c.tare);
                          }
                        },
                  child: const Text('Tare empty base'),
                ),
                FilledButton.tonal(
                  onPressed: !c.isConnected || c.busy
                      ? null
                      : () => showDialog<void>(
                          context: context,
                          builder: (_) => CalibrationDialog(controller: c),
                        ),
                  child: const Text('Calibrate with known weight'),
                ),
              ],
            ),
          ],
        ),
      ),
      InfoCard(
        title: '3  Choose your reminder rhythm',
        icon: Icons.notifications_active_outlined,
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            const Text(
              'Firmware defaults are 10:00–23:00, every 30 minutes. The values below are read from your bottle. Outside the window, it stays quiet.',
            ),
            const SizedBox(height: 16),
            if (s != null)
              SettingsEditor(controller: c, scheduleOnly: true)
            else
              const Text('Connect to load your active window.'),
          ],
        ),
      ),
      InfoCard(
        title: '4  Select your Classic phone',
        icon: Icons.phonelink_ring_outlined,
        child: PhoneSetup(controller: c),
      ),
    ];
  }

  List<Widget> _advanced() => [
    InfoCard(
      title: 'Device & link',
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(
            'Firmware: ${c.snapshot?.text('firmware') ?? '—'} · Protocol: ${c.snapshot?.text('protocol') ?? '—'}',
          ),
          Text(
            'Connection: ${c.connectionLabel} · MTU: ${c.isConnected ? c.mtu : '—'} · writes ≤20 bytes',
          ),
          Text(
            'Raw scale: ${c.snapshot?.text('raw', '—') ?? '—'} · baseline: ${c.snapshot?.text('baseline_g', '—') ?? '—'} g',
          ),
          Text(
            'Selected Classic: ${c.snapshot?.text('phone_mac', 'none') ?? '—'}',
          ),
          Text(
            'App BLE peer (diagnostic): ${c.snapshot?.text('app_peer_mac', 'none') ?? '—'}',
          ),
          const SizedBox(height: 8),
          const Text(
            'The app BLE peer is diagnostic only, separate from the selected Classic identity. Saving an identity is not proof of reachability.',
          ),
          TextButton(
            onPressed: !c.isConnected || c.busy
                ? null
                : () async {
                    if (await confirmAction(
                      context,
                      title: 'Clear selected Classic phone?',
                      message: 'Remove the saved Classic target and invalidate its presence. Alerts stay suppressed until you explicitly select another phone. The diagnostic BLE peer, networks and calibration are kept.',
                      confirmLabel: 'Clear Classic phone',
                    )) {
                      await _run(c.clearPhone);
                    }
                  },
            child: const Text('Clear selected Classic phone'),
          ),
        ],
      ),
    ),
    InfoCard(
      title: 'Flash storage (NVS)',
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          const Text(
            'Stored = persisted in flash. Firmware default = usable without an override. Scale tare/calibration need setup; classic_mac is required for alerts and classic_name is optional. BLE peer, Wi-Fi, baseline and legacy keys are diagnostic/optional, never presence. Secrets are never shown.',
          ),
          const SizedBox(height: 12),
          Align(
            alignment: Alignment.centerLeft,
            child: FilledButton.tonal(
              onPressed: !c.isConnected || c.busy
                  ? null
                  : () => _run(c.refreshNvs),
              child: Text(c.nvsLoading ? 'Reading…' : 'Read all NVS keys'),
            ),
          ),
          if (c.nvsError != null)
            Text(
              c.nvsError!,
              style: TextStyle(color: Theme.of(context).colorScheme.error),
            ),
          if (c.nvsComplete && c.nvsRequiredMissing.isNotEmpty)
            Text(
              'Scale setup needed: ${c.nvsRequiredMissing.join(', ')}. Use Setup to tare / calibrate.',
            ),
          if (c.nvsComplete && c.nvsUnreported.isNotEmpty)
            Text(
              '${c.nvsUnreported.length} known keys not reported by this firmware (not automatically faults).',
            ),
          const SizedBox(height: 12),
          for (final entry in c.nvsEntries.values) _nvsTile(entry),
          if (c.nvsComplete)
            for (final key in c.nvsUnreported)
              _nvsTile(NvsEntry(key, null, false), unreported: true),
        ],
      ),
    ),
    InfoCard(
      title: 'Live status snapshot',
      child: c.snapshot == null
          ? const Text('Connect to see live status.')
          : Column(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                for (final entry in c.snapshot!.data.entries)
                  if (entry.key != 'settings' && entry.key != 'networks')
                    Padding(
                      padding: const EdgeInsets.symmetric(vertical: 3),
                      child: Text(
                        '${entry.key}: ${c.safeText(entry.value.toString())}',
                        style: const TextStyle(
                          fontFamily: 'monospace',
                          fontSize: 12,
                        ),
                      ),
                    ),
              ],
            ),
    ),
    InfoCard(
      title: 'Command & event log',
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          const Text(
            'Newest first · bounded history · passwords and identity keys hidden. ACK means accepted, not necessarily internet success.',
          ),
          Align(
            alignment: Alignment.centerLeft,
            child: TextButton(
              onPressed: c.clearLog,
              child: const Text('Clear logs'),
            ),
          ),
          if (c.log.isEmpty) const Text('No commands yet.'),
          for (final line in c.log)
            Padding(
              padding: const EdgeInsets.symmetric(vertical: 4),
              child: Text(
                line,
                style: const TextStyle(fontFamily: 'monospace', fontSize: 12),
              ),
            ),
        ],
      ),
    ),
  ];

  Widget _nvsTile(NvsEntry entry, {bool unreported = false}) => Padding(
    padding: const EdgeInsets.symmetric(vertical: 8),
    child: Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text(
          entry.key,
          style: const TextStyle(
            fontWeight: FontWeight.w600,
            fontFamily: 'monospace',
            fontSize: 12,
          ),
        ),
        Text(
          '${c.safeText(entry.displayValue)} · ${unreported ? 'Not reported' : entry.classification}',
          style: TextStyle(
            color: !unreported && entry.requiredMissing
                ? Theme.of(context).colorScheme.error
                : null,
          ),
        ),
      ],
    ),
  );
}

class InfoCard extends StatelessWidget {
  const InfoCard({
    super.key,
    required this.title,
    required this.child,
    this.icon,
  });
  final String title;
  final Widget child;
  final IconData? icon;
  @override
  Widget build(BuildContext context) => Card(
    margin: const EdgeInsets.only(bottom: 16),
    child: Padding(
      padding: const EdgeInsets.all(22),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Row(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              if (icon != null) ...[
                Icon(icon, color: Theme.of(context).colorScheme.primary),
                const SizedBox(width: 10),
              ],
              Expanded(
                child: Text(
                  title,
                  style: Theme.of(context).textTheme.titleMedium
                      ?.copyWith(fontWeight: FontWeight.w700),
                ),
              ),
            ],
          ),
          const SizedBox(height: 14),
          child,
        ],
      ),
    ),
  );
}

class CalibrationDialog extends StatefulWidget {
  const CalibrationDialog({super.key, required this.controller});
  final BottleBleController controller;
  @override
  State<CalibrationDialog> createState() => _CalibrationDialogState();
}

class _CalibrationDialogState extends State<CalibrationDialog> {
  final _grams = TextEditingController();
  final _form = GlobalKey<FormState>();
  bool _confirmed = false;
  bool _saving = false;
  @override
  void dispose() {
    _grams.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) => AlertDialog(
    title: const Text('Calibrate safely'),
    content: SingleChildScrollView(
      child: Form(
        key: _form,
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            const Text(
              'Tare the empty base first. Place a known reference weight, keep it still, and enter its actual mass.',
            ),
            const SizedBox(height: 18),
            TextFormField(
              controller: _grams,
              keyboardType: const TextInputType.numberWithOptions(
                decimal: true,
              ),
              decoration: const InputDecoration(
                labelText: 'Reference weight (g)',
              ),
              validator: (value) {
                final n = double.tryParse(value ?? '');
                return n == null || !n.isFinite || n <= 0
                    ? 'Enter a positive weight.'
                    : null;
              },
            ),
            CheckboxListTile(
              contentPadding: EdgeInsets.zero,
              value: _confirmed,
              onChanged: _saving
                  ? null
                  : (v) => setState(() => _confirmed = v == true),
              title: const Text('Known weight is placed on the tared base'),
            ),
            if (_saving) const LinearProgressIndicator(),
            if (!_saving && widget.controller.bannerError != null)
              Text(
                widget.controller.bannerError!,
                style: TextStyle(color: Theme.of(context).colorScheme.error),
              ),
          ],
        ),
      ),
    ),
    actions: [
      TextButton(
        onPressed: _saving ? null : () => Navigator.pop(context),
        child: const Text('Cancel'),
      ),
      FilledButton(
        onPressed: !_confirmed || _saving
            ? null
            : () async {
                if (!_form.currentState!.validate()) return;
                setState(() => _saving = true);
                try {
                  await widget.controller.calibrate(double.parse(_grams.text));
                  if (context.mounted) Navigator.pop(context);
                } catch (_) {
                  if (mounted) setState(() => _saving = false);
                }
              },
        child: Text(_saving ? 'Waiting for bottle…' : 'Calibrate'),
      ),
    ],
  );
}

String _number(num? value) => value == null ? '—' : value.toStringAsFixed(0);
String _sourceLabel(String source) => switch (source) {
  'ntp' => 'internet (NTP)',
  'app' => 'app clock fallback',
  'rtc' => 'device clock',
  _ => 'not synced',
};
String _scaleLabel(String state) => switch (state) {
  'ready' => 'Ready',
  'need_tare' => 'Tare the empty base',
  'need_cal' => 'Calibrate with a known weight',
  'fault' => 'Sensor fault — check base / wiring',
  'no_data' => 'Waiting for sensor',
  _ => state,
};
String _stateLabel(String state) => switch (state) {
  'warming' => 'Getting ready',
  'idle' => 'Ready for your next sip',
  'missing' => 'Bottle away from base',
  'settling' => 'Letting the bottle settle',
  'reminder' => 'Time for a sip',
  'missing_alert' => 'Please return the bottle',
  'paused' => 'Reminders paused',
  'scale_error' => 'Scale needs attention',
  _ => state,
};

/// Prefer the countdown whenever the bottle reports one. Presence freshness is
/// checked automatically when an alert is due; it should not hide the schedule.
String _nextCheckLabel(BottleBleController c, BottleStatus? s) {
  if (s == null) return 'Waiting for app connection';
  final seconds = s.number('next_check_sec');
  final countdown = seconds == null
      ? null
      : 'In ${(seconds / 60).ceil().clamp(0, 9999)} min';
  if (!isClassicAddress(s.data['phone_mac'])) {
    return countdown == null
        ? 'Select a Classic phone in Setup'
        : '$countdown · select a Classic phone in Setup';
  }
  if (!s.flag('active')) {
    return countdown == null
        ? 'Outside the active window'
        : '$countdown · outside the active window';
  }
  if (countdown == null) return 'Waiting for device';
  if (c.presenceState == 'not_configured') {
    return '$countdown · select a Classic phone in Setup';
  }
  return countdown;
}

class TimeSetup extends StatefulWidget {
  const TimeSetup({super.key, required this.controller});
  final BottleBleController controller;
  @override
  State<TimeSetup> createState() => _TimeSetupState();
}

class _TimeSetupState extends State<TimeSetup> {
  final _timezone = TextEditingController();
  bool _dirty = false;
  @override
  void dispose() {
    _timezone.dispose();
    super.dispose();
  }

  int? _tz() {
    final n = int.tryParse(_timezone.text);
    if (n == null || n < -720 || n > 840 || n % 15 != 0) {
      widget.controller.reportError(
        'Timezone must be −720 to 840 minutes in 15-minute increments.',
      );
      return null;
    }
    return n;
  }

  Future<void> _run(Future<void> Function() fn) async {
    try {
      await fn();
    } catch (_) {}
  }

  @override
  Widget build(BuildContext context) {
    final c = widget.controller;
    final s = c.snapshot;
    if (!_dirty) _timezone.text = (s?.data['tz_min'] ?? '').toString();
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        Text(
          s == null
              ? 'Connect to read the device clock.'
              : 'Clock ${s.flag('clock_valid') ? 'ready' : 'not set'} · ${_sourceLabel(s.text('time_source'))}',
        ),
        const SizedBox(height: 12),
        const Text(
          'Choose internet time via a saved 2.4 GHz Wi-Fi network, or use this app’s clock as an explicitly labelled fallback. The bottle uses a fixed UTC offset; update it when daylight saving changes.',
        ),
        const SizedBox(height: 16),
        DropdownButtonFormField<int>(
          key: ValueKey('timezone-${_timezone.text}'),
          initialValue: int.tryParse(_timezone.text),
          isExpanded: true,
          menuMaxHeight: 320,
          decoration: const InputDecoration(
            labelText: 'Time zone (fixed UTC offset)',
            helperText:
                'Use the app timezone below, or choose your local offset.',
            helperMaxLines: 2,
          ),
          items: [
            for (var offset = -720; offset <= 840; offset += 15)
              DropdownMenuItem(
                value: offset,
                child: Text(
                  _timezoneLabel(offset),
                  maxLines: 1,
                  overflow: TextOverflow.ellipsis,
                ),
              ),
          ],
          onChanged: !c.isConnected || c.busy
              ? null
              : (value) => setState(() {
                  if (value != null) _timezone.text = value.toString();
                  _dirty = true;
                }),
        ),
        const SizedBox(height: 14),
        Wrap(
          spacing: 10,
          runSpacing: 10,
          children: [
            OutlinedButton(
              onPressed: !c.isConnected || c.busy
                  ? null
                  : () => setState(() {
                      _timezone.text = DateTime.now().timeZoneOffset.inMinutes
                          .toString();
                      _dirty = true;
                    }),
              child: const Text('Use app timezone'),
            ),
            OutlinedButton(
              onPressed: !c.isConnected || c.busy
                  ? null
                  : () async {
                      final tz = _tz();
                      if (tz == null) return;
                      await _run(() => c.saveSettings({'tz_min': tz}));
                    },
              child: const Text('Save timezone'),
            ),
            FilledButton(
              onPressed: !c.isConnected || c.busy
                  ? null
                  : () async {
                      final tz = _tz();
                      if (tz == null) return;
                      await _run(() => c.syncTime(timezoneMinutes: tz));
                    },
              child: const Text('Sync from app clock'),
            ),
          ],
        ),
        const SizedBox(height: 24),
        WifiSetup(controller: c),
      ],
    );
  }
}

String _timezoneLabel(int offset) {
  final absolute = offset.abs();
  final hours = (absolute ~/ 60).toString().padLeft(2, '0');
  final minutes = (absolute % 60).toString().padLeft(2, '0');
  const places = <int, String>{
    -600: 'Hawaii',
    -180: 'Argentina',
    0: 'UTC / Iceland',
    120: 'South Africa',
    180: 'Kenya / Saudi Arabia',
    240: 'UAE',
    330: 'India / Sri Lanka',
    345: 'Nepal',
    420: 'Thailand / Vietnam',
    480: 'Singapore / China',
    540: 'Japan / Korea',
    570: 'Darwin',
  };
  final place = places[offset];
  return 'UTC${offset < 0 ? '−' : '+'}$hours:$minutes${place == null ? '' : ' — $place'}';
}

class WifiSetup extends StatefulWidget {
  const WifiSetup({super.key, required this.controller});
  final BottleBleController controller;
  @override
  State<WifiSetup> createState() => _WifiSetupState();
}

class _WifiSetupState extends State<WifiSetup> {
  final _ssid = TextEditingController();
  final _password = TextEditingController();
  final _form = GlobalKey<FormState>();
  bool _saving = false;
  bool _manual = false;
  bool? _selectedSecured;

  void _selectNetwork(WifiNetwork network) {
    if (_ssid.text != network.ssid || _selectedSecured != network.secured) {
      _password.clear();
    }
    setState(() {
      _selectedSecured = network.secured;
      _ssid.text = network.ssid;
      _manual = false;
    });
  }

  Future<void> _search() async {
    try {
      await widget.controller.scanWifi();
    } catch (_) {
      // The controller exposes an actionable scan error next to Retry.
    }
  }

  @override
  void dispose() {
    _ssid.dispose();
    _password.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final c = widget.controller;
    final s = c.snapshot;
    final enabled = c.isConnected && !c.busy && !c.wifiTimeBusy && !_saving;
    return Form(
      key: _form,
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Text(
            'Internet time over Wi-Fi',
            style: Theme.of(context).textTheme.titleMedium,
          ),
          const SizedBox(height: 8),
          Text(
            'Wi-Fi: ${s?.text('wifi_state') ?? 'offline'}${(s?.text('wifi_ssid', '') ?? '').isEmpty ? '' : ' · ${s!.text('wifi_ssid')}'}',
          ),
          if (s != null && s.text('wifi_error', '').isNotEmpty)
            Text(
              'Wi-Fi error: ${c.safeText(s.text('wifi_error'))}',
              style: TextStyle(color: Theme.of(context).colorScheme.error),
            ),
          const SizedBox(height: 8),
          const Text(
            'Up to four saved networks. Spaces are preserved in names and passwords. Saving starts an asynchronous connection; accepted does not mean synced. Wi-Fi turns off after its bounded sync attempt.',
          ),
          const SizedBox(height: 16),
          const Text(
            'Search from the bottle’s location. Only 2.4 GHz networks are supported; 5 GHz-only networks cannot be used. Searching and selecting never save or connect.',
          ),
          const SizedBox(height: 12),
          Align(
            alignment: Alignment.centerLeft,
            child: FilledButton.icon(
              key: const Key('wifi-search'),
              onPressed: enabled ? _search : null,
              icon: c.scanBusy
                  ? const SizedBox(
                      width: 18,
                      height: 18,
                      child: CircularProgressIndicator(strokeWidth: 2),
                    )
                  : const Icon(Icons.search),
              label: Text(
                c.scanBusy
                    ? 'Searching…'
                    : c.scanError != null || c.scanComplete
                    ? 'Search again'
                    : 'Search networks',
              ),
            ),
          ),
          if (!c.isConnected)
            const Text('Connect to your bottle before searching.'),
          if (c.wifiTimeBusy)
            const Text(
              'Internet time sync is running. Wait for it to finish before searching or saving another network.',
            ),
          if (c.scanBusy)
            const Text(
              'The bottle is looking for nearby networks. This can take up to 25 seconds.',
            ),
          if (c.scanError != null) ...[
            Text(
              c.scanError!,
              style: TextStyle(color: Theme.of(context).colorScheme.error),
            ),
            const Text(
              'Move the bottle closer to the router and search again. Check that 2.4 GHz is enabled; a hidden network can be entered manually below.',
            ),
          ],
          if (c.scanComplete && !c.scanBusy) ...[
            if (c.wifiNetworks.isEmpty)
              const Text(
                'No visible networks found. Move the bottle closer to your router and search again. Enable 2.4 GHz (not 5 GHz-only), or enter a hidden network manually below.',
              ),
            for (final network in c.wifiNetworks)
              ListTile(
                key: ValueKey('wifi-result-${network.ssid}'),
                contentPadding: EdgeInsets.zero,
                enabled: enabled,
                selected: _ssid.text == network.ssid,
                leading: Icon(network.secured ? Icons.wifi_lock : Icons.wifi),
                title: Text(network.ssid),
                subtitle: Text(
                  '${network.signalLabel} · ${network.rssi} dBm\n${network.secured ? 'Password required' : 'Open network'}',
                ),
                trailing: _ssid.text == network.ssid
                    ? const Icon(Icons.check_circle_outline)
                    : null,
                onTap: enabled ? () => _selectNetwork(network) : null,
              ),
            if (c.scanTruncated)
              const Text(
                'Showing the 15 strongest network names. Move the bottle closer to your router and retry if yours is missing.',
              ),
            if (c.scanHiddenCount > 0)
              const Text(
                'Hidden networks were also detected. Use hidden/manual entry if needed.',
              ),
          ],
          TextButton.icon(
            key: const Key('wifi-manual-toggle'),
            onPressed: enabled
                ? () => setState(() => _manual = !_manual)
                : null,
            icon: Icon(_manual ? Icons.expand_less : Icons.expand_more),
            label: const Text('Hidden / manual network'),
          ),
          if (_manual)
            TextFormField(
              key: const Key('wifi-ssid'),
              controller: _ssid,
              enabled: enabled,
              autocorrect: false,
              enableSuggestions: false,
              decoration: const InputDecoration(
                labelText: 'Network name (SSID)',
              ),
              onChanged: (_) => setState(() {
                _password.clear();
                _selectedSecured = null;
              }),
              validator: (v) => BottleProtocol.validateWifi(v ?? '', ''),
            )
          else
            InputDecorator(
              decoration: const InputDecoration(labelText: 'Selected network'),
              child: Text(
                _ssid.text.isEmpty ? 'Choose a network above' : _ssid.text,
              ),
            ),
          const SizedBox(height: 12),
          TextFormField(
            key: const Key('wifi-password'),
            controller: _password,
            enabled: enabled && _ssid.text.isNotEmpty,
            obscureText: true,
            autocorrect: false,
            enableSuggestions: false,
            decoration: const InputDecoration(
              labelText: 'Wi-Fi password',
              helperText: 'Leave empty only for an open network',
              helperMaxLines: 2,
            ),
            validator: (v) {
              if (_selectedSecured == true && (v ?? '').isEmpty) {
                return 'Enter this network’s Wi-Fi password.';
              }
              return BottleProtocol.validateWifi(_ssid.text, v ?? '');
            },
          ),
          const SizedBox(height: 14),
          Wrap(
            spacing: 10,
            runSpacing: 10,
            children: [
              FilledButton.tonalIcon(
                onPressed: !enabled || _ssid.text.isEmpty
                    ? null
                    : () async {
                        if (!_form.currentState!.validate()) return;
                        setState(() => _saving = true);
                        try {
                          await c.addWifi(_ssid.text, _password.text);
                          if (mounted) _password.clear();
                        } catch (_) {
                          /* Do not claim success; retain draft to retry. */
                        } finally {
                          if (mounted) setState(() => _saving = false);
                        }
                      },
                icon: const Icon(Icons.wifi),
                label: Text(_saving ? 'Waiting for bottle…' : 'Save and sync'),
              ),
              OutlinedButton(
                onPressed: !enabled
                    ? null
                    : () async {
                        try {
                          await c.syncWifi();
                        } catch (_) {}
                      },
                child: const Text('Sync internet time now'),
              ),
            ],
          ),
          const SizedBox(height: 14),
          Text(
            'Saved networks (${s?.networks.length ?? 0}/4)',
            style: const TextStyle(fontWeight: FontWeight.w600),
          ),
          if (s?.networks.isEmpty ?? true)
            const Text(
              'No networks reported. App clock sync works without Wi-Fi.',
            ),
          for (final network in s?.networks ?? <Map<String, dynamic>>[])
            ListTile(
              contentPadding: EdgeInsets.zero,
              leading: Icon(
                network['secured'] == true ? Icons.wifi_lock : Icons.wifi,
              ),
              title: Text(network['ssid']?.toString() ?? 'Unknown network'),
              subtitle: Text(
                network['secured'] == true
                    ? 'Password saved (never read back)'
                    : 'Open network',
              ),
              trailing: IconButton(
                tooltip: 'Remove network',
                onPressed: !enabled
                    ? null
                    : () async {
                        final ssid = network['ssid'].toString();
                        if (await confirmAction(
                          context,
                          title: 'Remove network?',
                          message:
                              'Remove “$ssid” from the bottle? You will need its password to add it again.',
                          confirmLabel: 'Remove',
                        )) {
                          try {
                            await c.removeWifi(ssid);
                          } catch (_) {}
                        }
                      },
                icon: const Icon(Icons.delete_outline),
              ),
            ),
        ],
      ),
    );
  }
}
