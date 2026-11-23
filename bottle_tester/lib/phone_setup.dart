import 'package:material_ui/material_ui.dart';

import 'bottle_ble_controller.dart';
import 'action_confirmation.dart';

String presenceLabel(BottleBleController c) {
  final label = switch (c.presenceState) {
    'not_configured' => 'Not configured · select a Classic phone for alerts',
    'checking' => 'Checking selected phone…',
    'present' => 'Present · selected phone replied over Classic',
    'away' => 'Away · selected phone did not reply',
    'timeout' => 'Phone check timed out · retry',
    'radio_error' =>
      'Classic radio error · retry after radio activity finishes',
    'scanning' => 'Searching Classic devices · alerts may be suppressed',
    'stalled' => 'Classic radio stalled · reconnect or restart the bottle',
    _ => 'Unknown · waiting for a fresh Classic check',
  };
  return 'App BLE link: ${c.isConnected ? 'connected (configuration only)' : 'offline'}\nSelected phone: $label';
}

/// Classic discovery is performed by the bottle, never by the app BLE scanner.
class PhoneSetup extends StatelessWidget {
  const PhoneSetup({super.key, required this.controller});
  final BottleBleController controller;

  Future<void> _run(Future<void> Function() action) async {
    try {
      await action();
    } catch (_) {
      /* Controller owns visible errors. */
    }
  }

  @override
  Widget build(BuildContext context) {
    final c = controller;
    final s = c.snapshot;
    final enabled = c.isConnected && !c.busy && !c.wifiTimeBusy;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        const Text(
          'Presence is always on (read-only). Alerts need a selected Classic phone and a fresh successful check. The app BLE connection is configuration only; the app does not need to stay open.',
        ),
        const SizedBox(height: 12),
        Text(presenceLabel(c)),
        if (s?.data['phone_mac'] != null)
          Text(
            'Saved Classic identity: ${s!.text('phone_name', 'Unnamed Classic device')}\n${s.text('phone_mac')}',
          ),
        if (s?.text('presence_error', '').isNotEmpty == true)
          Text(
            'Phone error: ${c.safeText(s!.text('presence_error'))}',
            style: TextStyle(color: Theme.of(context).colorScheme.error),
          ),
        const SizedBox(height: 12),
        Text(
          'For first discovery, open your phone’s system Bluetooth settings and keep that screen open during Search phones. ${c.usesNativeClassicDiscovery ? 'This Mac searches Bluetooth Classic devices' : 'The bottle searches Bluetooth Classic devices'} for about 10 seconds (allow up to 30). Names can be shared: confirm the exact identity before saving. Addresses below are secondary diagnostics, not a manual entry requirement.',
        ),
        const SizedBox(height: 12),
        const Text(
          'Targeted Classic remote-name checks have been tested with a locked Samsung phone in the background. iPhone hardware testing is pending; support is not universal. Discovery and radio activity can temporarily suppress alerts.',
        ),
        const SizedBox(height: 12),
        Wrap(
          spacing: 10,
          runSpacing: 10,
          children: [
            FilledButton.icon(
              key: const Key('phone-search'),
              onPressed: enabled ? () => _run(c.scanPhones) : null,
              icon: const Icon(Icons.search),
              label: Text(
                c.phoneScanBusy ? 'Searching phones…' : 'Search phones',
              ),
            ),
            OutlinedButton(
              key: const Key('phone-probe'),
              onPressed: enabled && s?.data['phone_mac'] != null
                  ? () => _run(c.probePhone)
                  : null,
              child: Text(
                c.phoneProbeBusy ? 'Checking…' : 'Check selected phone',
              ),
            ),
            TextButton(
              onPressed: enabled && s?.data['phone_mac'] != null
                  ? () async {
                      if (await confirmAction(
                        context,
                        title: 'Clear selected Classic phone?',
                        message:
                            'Remove ${s!.text('phone_name', 'selected phone')} (${s.text('phone_mac')})? Alerts require a new selection. Diagnostic BLE peer, calibration and networks are kept.',
                        confirmLabel: 'Clear Classic phone',
                      )) {
                        await _run(c.clearPhone);
                      }
                    }
                  : null,
              child: const Text('Clear selected Classic phone'),
            ),
          ],
        ),
        if (c.phoneError != null)
          Text(
            c.phoneError!,
            style: TextStyle(color: Theme.of(context).colorScheme.error),
          ),
        if (c.phoneScanId != null && c.phones.isEmpty)
          const Text(
            'No discoverable Classic phones found. Open phone Bluetooth settings, move closer and search again. No identity was selected.',
          ),
        for (final phone in c.phones)
          ListTile(
            key: ValueKey('phone-result-${phone.address}'),
            contentPadding: EdgeInsets.zero,
            title: Text(phone.label),
            subtitle: Text(
              '${phone.rssi == -127 ? 'Signal unavailable' : '${phone.rssi} dBm'}\nClassic address (diagnostic): ${phone.address}',
            ),
            onTap: !enabled
                ? null
                : () async {
                    final scanId = c.phoneScanId;
                    if (scanId == null) return;
                    final previous = s?.data['phone_mac'];
                    final confirmed = await confirmAction(
                      context,
                      title: previous == null
                          ? 'Save this Classic phone?'
                          : 'Replace selected Classic phone?',
                      message:
                          'Save ${phone.label} (${phone.address}) as the Classic presence target?${previous == null ? '' : '\nThis replaces ${s!.text('phone_name', 'selected phone')} ($previous) and clears its old presence.'}\nThe bottle verifies this address before saving. Future alerts still require fresh automatic checks.',
                      confirmLabel: previous == null
                          ? 'Save Classic phone'
                          : 'Replace Classic phone',
                    );
                    if (confirmed) {
                      await _run(() => c.selectPhone(phone.address, scanId));
                    }
                  },
          ),
        if (c.phoneScanTruncated)
          const Text(
            'Showing the 8 strongest unique Classic devices. Move closer to your phone and search again if missing.',
          ),
      ],
    );
  }
}
