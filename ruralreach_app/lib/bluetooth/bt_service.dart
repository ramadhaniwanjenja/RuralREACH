import 'dart:typed_data';
import 'package:flutter_bluetooth_serial/flutter_bluetooth_serial.dart';

/// Wraps the Bluetooth Serial (SPP) link between the phone and the
/// Heltec ESP32 LoRa node. The node firmware listens on the same
/// classic-Bluetooth SPP profile, so anything sent here is read
/// directly by the node's firmware.
class BtService {
  BtService._();
  static final BtService instance = BtService._();

  BluetoothConnection? _connection;
  bool get isConnected => _connection != null && _connection!.isConnected;

  /// Call once after the user pairs the Heltec board in Android
  /// Bluetooth settings, passing in its MAC address.
  Future<void> connectToNode(String deviceAddress) async {
    _connection = await BluetoothConnection.toAddress(deviceAddress);
  }

  /// Sends "SMS|<phone>|<message>" to the node. The node firmware
  /// encrypts it with AES-128 and forwards it over LoRa to the gateway.
  Future<void> sendSms(String phone, String message) async {
    if (!isConnected) return;
    final line = 'SMS|$phone|$message\n';
    _connection!.output.add(Uint8List.fromList(line.codeUnits));
    await _connection!.output.allSent;
  }

  /// Tells the node "push-to-talk pressed" — it starts sampling the
  /// I2S microphone and streaming Codec2 frames over LoRa.
  Future<void> startVoiceSession() async {
    if (!isConnected) return;
    _connection!.output.add(Uint8List.fromList('PTT_START\n'.codeUnits));
    await _connection!.output.allSent;
  }

  /// Tells the node "push-to-talk released" — voice session ends.
  Future<void> endVoiceSession() async {
    if (!isConnected) return;
    _connection!.output.add(Uint8List.fromList('PTT_END\n'.codeUnits));
    await _connection!.output.allSent;
  }
}
