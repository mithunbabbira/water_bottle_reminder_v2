import 'package:flutter/foundation.dart';
import 'package:material_ui/material_ui.dart';

import 'bottle_ble_controller.dart';
import 'bottle_protocol.dart';

class SettingsEditor extends StatefulWidget {
  const SettingsEditor({
    super.key,
    required this.controller,
    this.scheduleOnly = false,
  });
  final BottleBleController controller;
  final bool scheduleOnly;
  @override
  State<SettingsEditor> createState() => _SettingsEditorState();
}

class _SettingsEditorState extends State<SettingsEditor> {
  final _form = GlobalKey<FormState>();
  final Map<String, TextEditingController> _fields = {};
  final Set<String> _dirty = {};
  Map<String, dynamic> _source = {};
  bool? _dnd;
  int? _beep;
  bool _saving = false;
  static const _labels = {
    'start_min': 'Active from (HH:mm)',
    'end_min': 'Active until (HH:mm)',
    'interval_min': 'Reminder interval (minutes)',
    'missing_sec': 'Bottle-away timeout (seconds)',
    'beep_delay_sec': 'Beep delay (seconds)',
    'drink_grams': 'Minimum sip (grams ≈ ml)',
    'settle_ms': 'Scale settling time (milliseconds)',
    'reminder_color': 'Sip reminder color (#RRGGBB)',
    'missing_color': 'Missing bottle color (#RRGGBB)',
  };
  static const _helpers = {
    'start_min': '24-hour local time',
    'end_min': 'May cross midnight; equal times disable alerts',
    'interval_min': '1–240 minutes',
    'missing_sec': '10–3600 seconds away before alert',
    'beep_delay_sec': '0–30 seconds after the light',
    'drink_grams': '5–500 grams; smaller changes are ignored',
    'settle_ms': '500–5000 ms; wait for a stable measurement',
    'reminder_color': '000000 turns the reminder light off',
    'missing_color': 'Independent of the sip reminder light',
  };
  static const _palette = <(String, int)>[
    ('Aqua', 0x00c8ff),
    ('Violet', 0x8000ff),
    ('Amber', 0xffb400),
    ('Green', 0x28dc50),
    ('Rose', 0xff2850),
    ('Off', 0),
  ];

  @override
  void initState() {
    super.initState();
    for (final key in _labels.keys) {
      _fields[key] = TextEditingController();
    }
    _load();
  }

  @override
  void didUpdateWidget(covariant SettingsEditor oldWidget) {
    super.didUpdateWidget(oldWidget);
    final incoming = widget.controller.snapshot?.settings ?? {};
    if (_dirty.isEmpty && !_saving && !mapEquals(_source, incoming)) _load();
  }

  void _load() {
    _source = widget.controller.snapshot?.settings ?? {};
    for (final entry in _fields.entries) {
      final value = _source[entry.key];
      entry.value.text = value == null
          ? ''
          : entry.key.endsWith('_color') && value is int
          ? value.toRadixString(16).padLeft(6, '0').toUpperCase()
          : (entry.key == 'start_min' || entry.key == 'end_min') && value is int
          ? '${(value ~/ 60).toString().padLeft(2, '0')}:${(value % 60).toString().padLeft(2, '0')}'
          : value.toString();
    }
    _dnd = _source['dnd'] is bool ? _source['dnd'] as bool : null;
    _beep =
        _source['beep_type'] is int &&
            (_source['beep_type'] as int) >= 0 &&
            (_source['beep_type'] as int) <= 3
        ? _source['beep_type'] as int
        : null;
  }

  @override
  void dispose() {
    for (final field in _fields.values) {
      field.dispose();
    }
    super.dispose();
  }

  int? _parse(String key) {
    final text = _fields[key]!.text;
    if (key == 'start_min' || key == 'end_min') {
      final match = RegExp(r'^(\d{1,2}):(\d{2})$').firstMatch(text);
      if (match == null) return null;
      final hours = int.parse(match[1]!);
      final minutes = int.parse(match[2]!);
      return hours <= 23 && minutes <= 59 ? hours * 60 + minutes : null;
    }
    if (key.endsWith('_color')) {
      final value = text.startsWith('#') ? text.substring(1) : text;
      return RegExp(r'^[0-9a-fA-F]{6}$').hasMatch(value)
          ? int.parse(value, radix: 16)
          : null;
    }
    return int.tryParse(text);
  }

  String? _validate(String key) {
    if (!_dirty.contains(key)) return null;
    final value = _parse(key);
    if (value == null) {
      return key.endsWith('_color')
          ? 'Enter six hex digits.'
          : key == 'start_min' || key == 'end_min'
          ? 'Use HH:mm (00:00–23:59).'
          : 'Enter a whole number.';
    }
    return BottleProtocol.validateSettings({key: value});
  }

  void _changed(String key) => setState(() => _dirty.add(key));

  Future<void> _save() async {
    if (!_form.currentState!.validate()) return;
    final fields = <String, dynamic>{};
    for (final key in _dirty) {
      fields[key] = key == 'dnd'
          ? _dnd
          : key == 'beep_type'
          ? _beep
          : _parse(key);
    }
    setState(() => _saving = true);
    try {
      await widget.controller.saveSettings(fields);
      if (!mounted) return;
      setState(() {
        _dirty.clear();
        _load();
      });
    } catch (_) {
      /* Keep unsaved drafts and the controller's visible error. */
    } finally {
      if (mounted) setState(() => _saving = false);
    }
  }

  @override
  Widget build(BuildContext context) {
    final enabled =
        widget.controller.isConnected && !widget.controller.busy && !_saving;
    final keys = widget.scheduleOnly
        ? ['start_min', 'end_min', 'interval_min']
        : _labels.keys.toList();
    final content = Form(
      key: _form,
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Text(
            _saving
                ? 'Saving · waiting for ACK and device readback…'
                : _dirty.isEmpty
                ? 'Device values loaded · edit, then save'
                : 'Unsaved changes · not yet applied to bottle',
            style: TextStyle(
              color: Theme.of(context).colorScheme.primary,
              fontWeight: FontWeight.w600,
            ),
          ),
          const SizedBox(height: 20),
          LayoutBuilder(
            builder: (context, constraints) {
              final width = constraints.maxWidth >= 600
                  ? (constraints.maxWidth - 18) / 2
                  : constraints.maxWidth;
              return Wrap(
                spacing: 18,
                runSpacing: 20,
                children: [
                  for (final key in keys)
                    SizedBox(
                      width: width,
                      child: Column(
                        crossAxisAlignment: CrossAxisAlignment.start,
                        children: [
                          TextFormField(
                            key: ValueKey('setting-$key'),
                            controller: _fields[key],
                            enabled: enabled,
                            keyboardType:
                                key.endsWith('_color') ||
                                    key == 'start_min' ||
                                    key == 'end_min'
                                ? TextInputType.text
                                : TextInputType.number,
                            decoration: InputDecoration(
                              labelText: _labels[key],
                              helperText: _source[key] == null
                                  ? 'Not reported by device; no value assumed'
                                  : _helpers[key],
                              helperMaxLines: 3,
                              errorMaxLines: 3,
                            ),
                            onChanged: (_) => _changed(key),
                            validator: (_) => _validate(key),
                          ),
                          if (key.endsWith('_color')) ...[
                            const SizedBox(height: 10),
                            Wrap(
                              spacing: 6,
                              runSpacing: 6,
                              children: [
                                for (final color in _palette)
                                  Tooltip(
                                    message: color.$1,
                                    child: ActionChip(
                                      avatar: CircleAvatar(
                                        backgroundColor: Color(
                                          0xff000000 | color.$2,
                                        ),
                                        radius: 7,
                                      ),
                                      label: Text(color.$1),
                                      onPressed: !enabled
                                          ? null
                                          : () {
                                              _fields[key]!.text = color.$2
                                                  .toRadixString(16)
                                                  .padLeft(6, '0')
                                                  .toUpperCase();
                                              _changed(key);
                                            },
                                    ),
                                  ),
                              ],
                            ),
                          ],
                        ],
                      ),
                    ),
                ],
              );
            },
          ),
          if (!widget.scheduleOnly) ...[
            const SizedBox(height: 24),
            DropdownButtonFormField<int>(
              key: ValueKey('beep-$_beep'),
              initialValue: _beep,
              isExpanded: true,
              decoration: const InputDecoration(labelText: 'Alert sound'),
              hint: const Text('Not reported by device'),
              items: const [
                DropdownMenuItem(value: 0, child: Text('Off')),
                DropdownMenuItem(value: 1, child: Text('Single beep')),
                DropdownMenuItem(value: 2, child: Text('Double beep')),
                DropdownMenuItem(value: 3, child: Text('Long beep')),
              ],
              onChanged: !enabled
                  ? null
                  : (value) {
                      _beep = value;
                      _changed('beep_type');
                    },
            ),
            SwitchListTile.adaptive(
              contentPadding: EdgeInsets.zero,
              title: const Text('Mute sound (Do not disturb)'),
              subtitle: Text(
                _dnd == null
                    ? 'Not reported by device'
                    : 'Keeps reminder lights on, but silences the buzzer',
              ),
              value: _dnd == true,
              onChanged: !enabled || _dnd == null
                  ? null
                  : (value) {
                      _dnd = value;
                      _changed('dnd');
                    },
            ),
            const SizedBox(height: 8),
            const Text(
              'Always-on presence information and timezone are in Setup. The legacy “color” NVS field is shown in Advanced; reminder and missing-bottle colors are independently controlled here.',
            ),
          ],
          const SizedBox(height: 20),
          Wrap(
            spacing: 10,
            runSpacing: 10,
            children: [
              FilledButton(
                onPressed: enabled && _dirty.isNotEmpty ? _save : null,
                child: Text(
                  _saving
                      ? 'Saving…'
                      : widget.scheduleOnly
                      ? 'Save reminder rhythm'
                      : 'Save settings',
                ),
              ),
              if (_dirty.isNotEmpty)
                TextButton(
                  onPressed: !enabled
                      ? null
                      : () => setState(() {
                          _dirty.clear();
                          _load();
                        }),
                  child: const Text('Discard edits'),
                ),
            ],
          ),
        ],
      ),
    );
    if (widget.scheduleOnly) return content;
    return Card(
      margin: const EdgeInsets.only(bottom: 16),
      child: Padding(padding: const EdgeInsets.all(22), child: content),
    );
  }
}
