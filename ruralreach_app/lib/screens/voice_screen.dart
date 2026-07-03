import 'package:flutter/material.dart';
import '../bluetooth/bt_service.dart';

class VoiceScreen extends StatefulWidget {
  const VoiceScreen({super.key});

  @override
  State<VoiceScreen> createState() => _VoiceScreenState();
}

class _VoiceScreenState extends State<VoiceScreen> {
  final _phoneController = TextEditingController();
  bool _talking = false;
  bool _inCall = false;

  static const Color brandBlue = Color(0xFF1D4ED8);
  static const Color lightBlue = Color(0xFFEFF4FF);

  Future<void> _call() async {
    if (_phoneController.text.isEmpty) return;
    await BtService.instance.startCall(_phoneController.text);
    setState(() => _inCall = true);
  }

  Future<void> _hangup() async {
    await BtService.instance.endCall();
    setState(() {
      _inCall = false;
      _talking = false;
    });
  }

  void _startTalking() {
    setState(() => _talking = true);
    BtService.instance.startVoiceSession();
  }

  void _stopTalking() {
    setState(() => _talking = false);
    BtService.instance.endVoiceSession();
  }

  @override
  void dispose() {
    _phoneController.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        backgroundColor: brandBlue,
        title: const Text('Voice Call'),
      ),
      body: SingleChildScrollView(
        padding: const EdgeInsets.all(20),
        child: Column(
          children: [
            // ── phone number + Call / Hang up ──
            TextField(
              controller: _phoneController,
              keyboardType: TextInputType.phone,
              enabled: !_inCall,
              decoration: InputDecoration(
                labelText: 'Receiver number',
                hintText: '+250 7..',
                border: OutlineInputBorder(
                  borderRadius: BorderRadius.circular(10),
                ),
              ),
            ),
            const SizedBox(height: 12),
            SizedBox(
              width: double.infinity,
              height: 48,
              child: ElevatedButton.icon(
                onPressed: _inCall ? _hangup : _call,
                icon: Icon(_inCall ? Icons.call_end : Icons.call),
                label: Text(_inCall ? 'Hang up' : 'Call'),
                style: ElevatedButton.styleFrom(
                  backgroundColor: _inCall ? Colors.red : Colors.green,
                  foregroundColor: Colors.white,
                  shape: RoundedRectangleBorder(
                      borderRadius: BorderRadius.circular(10)),
                ),
              ),
            ),
            const SizedBox(height: 24),
            Text(
              !_inCall
                  ? 'Enter a number and press Call'
                  : _talking
                      ? 'Talking...'
                      : 'Hold to talk',
              style: const TextStyle(color: Colors.grey, fontSize: 16),
            ),
            const SizedBox(height: 20),
            // ── push-to-talk (only meaningful during a call) ──
            GestureDetector(
              onTapDown: _inCall ? (_) => _startTalking() : null,
              onTapUp: _inCall ? (_) => _stopTalking() : null,
              onTapCancel: _inCall ? () => _stopTalking() : null,
              child: Opacity(
                opacity: _inCall ? 1.0 : 0.4,
                child: Container(
                  width: 200,
                  height: 200,
                  decoration: BoxDecoration(
                    shape: BoxShape.circle,
                    color: lightBlue,
                    border: Border.all(color: brandBlue, width: 3),
                  ),
                  child: Column(
                    mainAxisAlignment: MainAxisAlignment.center,
                    children: [
                      Icon(_talking ? Icons.mic : Icons.mic_none,
                          size: 56, color: brandBlue),
                      const SizedBox(height: 10),
                      Text(
                        _talking ? 'RELEASE' : 'PUSH',
                        style: const TextStyle(
                          fontSize: 16,
                          fontWeight: FontWeight.w700,
                          color: brandBlue,
                        ),
                      ),
                    ],
                  ),
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }
}
