import 'dart:async';
import 'dart:typed_data';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';

/// Wraps the BLE link between the phone and the Heltec ESP32-S3 LoRa node.
///
/// The Heltec WiFi LoRa 32 V3 is an ESP32-S3, which supports Bluetooth Low
/// Energy only (no Classic Bluetooth / SPP). The node firmware runs a Nordic
/// UART Service (NUS) and advertises as "RuralReach"; we connect to it and
/// write newline-terminated command lines to the RX characteristic.
class BtService {
  BtService._();
  static final BtService instance = BtService._();

  // Must match the UUIDs in the node firmware (main.c).
  static final Guid _nusService = Guid('6e400001-b5a3-f393-e0a9-e50e24dcca9e');
  static final Guid _nusRx = Guid('6e400002-b5a3-f393-e0a9-e50e24dcca9e');
  // Node -> phone notify channel (replies forwarded from the gateway).
  static final Guid _nusTx = Guid('6e400003-b5a3-f393-e0a9-e50e24dcca9e');

  static const String deviceName = 'RuralReach';

  BluetoothDevice? _device;
  BluetoothCharacteristic? _rx;
  BluetoothCharacteristic? _tx;
  StreamSubscription<List<int>>? _txSub;

  // Broadcasts incoming messages from the node, as raw "sender|body" strings.
  final StreamController<String> _incoming = StreamController<String>.broadcast();
  Stream<String> get incomingMessages => _incoming.stream;

  bool get isConnected => _device != null && _device!.isConnected;

  /// Scans for the node advertising as "RuralReach", connects, and locates
  /// the NUS RX characteristic. Returns true once ready to send.
  Future<bool> connectToNode({
    Duration timeout = const Duration(seconds: 12),
  }) async {
    if (isConnected && _rx != null) return true;

    final found = Completer<BluetoothDevice?>();
    final sub = FlutterBluePlus.scanResults.listen((results) {
      for (final r in results) {
        // Match by name. The node advertises its name only (the 128-bit NUS
        // UUID doesn't fit alongside the name in the 31-byte adv packet), so
        // we can't filter the scan by service UUID.
        final name = r.device.platformName.isNotEmpty
            ? r.device.platformName
            : r.advertisementData.advName;
        if (name == deviceName && !found.isCompleted) {
          found.complete(r.device);
        }
      }
    });

    await FlutterBluePlus.startScan(timeout: timeout);
    final device =
        await found.future.timeout(timeout, onTimeout: () => null);
    await FlutterBluePlus.stopScan();
    await sub.cancel();

    if (device == null) return false;

    await device.connect(timeout: const Duration(seconds: 10));
    _device = device;

    for (final service in await device.discoverServices()) {
      if (service.uuid == _nusService) {
        for (final c in service.characteristics) {
          if (c.uuid == _nusRx) _rx = c;
          if (c.uuid == _nusTx) _tx = c;
        }
      }
    }

    // Subscribe to node -> phone notifications (replies from the gateway).
    if (_tx != null) {
      await _tx!.setNotifyValue(true);
      await _txSub?.cancel();
      _txSub = _tx!.onValueReceived.listen((data) {
        if (data.isEmpty) return;
        _incoming.add(String.fromCharCodes(data));
      });
    }
    return _rx != null;
  }

  Future<void> _sendLine(String line) async {
    final c = _rx;
    if (c == null || !isConnected) return;
    await c.write(
      Uint8List.fromList(line.codeUnits),
      withoutResponse: c.properties.writeWithoutResponse,
    );
  }

  /// Sends `SMS|<phone>|<message>` to the node, which (for now) shows it on
  /// the OLED; later it will be AES-encrypted and forwarded over LoRa.
  Future<void> sendSms(String phone, String message) =>
      _sendLine('SMS|$phone|$message\n');

  /// Tells the node "push-to-talk pressed".
  Future<void> startVoiceSession() => _sendLine('PTT_START\n');

  /// Tells the node "push-to-talk released".
  Future<void> endVoiceSession() => _sendLine('PTT_END\n');

  /// Asks the gateway to place a GSM voice call to [phone].
  Future<void> startCall(String phone) => _sendLine('CALL|$phone\n');

  /// Asks the gateway to hang up the GSM call.
  Future<void> endCall() => _sendLine('ENDCALL\n');

  Future<void> disconnect() async {
    await _txSub?.cancel();
    _txSub = null;
    await _device?.disconnect();
    _device = null;
    _rx = null;
    _tx = null;
  }
}
