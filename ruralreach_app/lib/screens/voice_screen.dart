import 'package:flutter/material.dart';
import '../bluetooth/bt_service.dart';

class VoiceScreen extends StatefulWidget {
  const VoiceScreen({super.key});

  @override
  State<VoiceScreen> createState() => _VoiceScreenState();
}

class _VoiceScreenState extends State<VoiceScreen> {
  bool _talking = false;

  static const Color brandBlue = Color(0xFF1D4ED8);
  static const Color lightBlue = Color(0xFFEFF4FF);

  void _startTalking() {
    setState(() => _talking = true);
    BtService.instance.startVoiceSession();
  }

  void _stopTalking() {
    setState(() => _talking = false);
    BtService.instance.endVoiceSession();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        backgroundColor: brandBlue,
        title: const Text('Voice Call'),
      ),
      body: Column(
        children: [
          const SizedBox(height: 30),
          Text(
            _talking ? 'Talking...' : 'Hold to talk',
            style: const TextStyle(color: Colors.grey, fontSize: 16),
          ),
          const SizedBox(height: 40),
          GestureDetector(
            onTapDown: (_) => _startTalking(),
            onTapUp: (_) => _stopTalking(),
            onTapCancel: () => _stopTalking(),
            child: Container(
              width: 220,
              height: 220,
              decoration: BoxDecoration(
                shape: BoxShape.circle,
                color: lightBlue,
                border: Border.all(color: brandBlue, width: 3),
              ),
              child: Column(
                mainAxisAlignment: MainAxisAlignment.center,
                children: [
                  Icon(
                    _talking ? Icons.mic : Icons.mic_none,
                    size: 60,
                    color: brandBlue,
                  ),
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
          const SizedBox(height: 40),
          const Row(
            mainAxisAlignment: MainAxisAlignment.center,
            children: [
              Icon(Icons.network_cell, size: 18, color: Colors.grey),
              SizedBox(width: 6),
              Text('Signal: good', style: TextStyle(color: Colors.grey)),
            ],
          ),
        ],
      ),
    );
  }
}
