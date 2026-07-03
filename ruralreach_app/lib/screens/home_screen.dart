import 'dart:async';
import 'package:flutter/material.dart';
import '../bluetooth/bt_service.dart';
import 'sms_screen.dart';
import 'voice_screen.dart';

class HomeScreen extends StatefulWidget {
  const HomeScreen({super.key});

  @override
  State<HomeScreen> createState() => _HomeScreenState();
}

class _HomeScreenState extends State<HomeScreen> {
  bool isConnected = false;
  bool _connecting = false;
  String language = 'Kinyarwanda';
  String? _replyFrom;
  String? _replyBody;
  StreamSubscription<String>? _replySub;

  static const Color brandBlue = Color(0xFF1D4ED8);
  static const Color lightGray = Color(0xFFD4D4D4);

  @override
  void initState() {
    super.initState();
    // Show replies pushed from the node (forwarded by the gateway over LoRa).
    _replySub = BtService.instance.incomingMessages.listen((raw) {
      final bar = raw.indexOf('|');
      final from = bar > 0 ? raw.substring(0, bar) : 'Reply';
      final body = bar > 0 ? raw.substring(bar + 1) : raw;
      if (!mounted) return;
      setState(() {
        _replyFrom = from.trim();
        _replyBody = body.trim();
      });
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text('Reply from ${from.trim()}: ${body.trim()}')),
      );
    });
  }

  @override
  void dispose() {
    _replySub?.cancel();
    super.dispose();
  }

  Future<void> _connect() async {
    setState(() => _connecting = true);
    final ok = await BtService.instance.connectToNode();
    if (!mounted) return;
    setState(() {
      _connecting = false;
      isConnected = ok;
    });
    ScaffoldMessenger.of(context).showSnackBar(
      SnackBar(
        content: Text(ok
            ? 'Connected to RuralReach node'
            : 'Node not found — is it powered and advertising?'),
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        backgroundColor: brandBlue,
        elevation: 0,
        title: const Text('RuralReach',
            style: TextStyle(fontWeight: FontWeight.w600)),
        actions: [
          Padding(
            padding: const EdgeInsets.only(right: 16),
            child: Center(
              child: Container(
                width: 10,
                height: 10,
                decoration: BoxDecoration(
                  color: isConnected ? Colors.green : Colors.grey,
                  shape: BoxShape.circle,
                ),
              ),
            ),
          ),
        ],
      ),
      body: Column(
        children: [
          InkWell(
            onTap: (_connecting || isConnected) ? null : _connect,
            child: Padding(
              padding:
                  const EdgeInsets.symmetric(vertical: 10, horizontal: 16),
              child: Row(
                children: [
                  if (_connecting) ...[
                    const SizedBox(
                      width: 14,
                      height: 14,
                      child: CircularProgressIndicator(strokeWidth: 2),
                    ),
                    const SizedBox(width: 8),
                  ],
                  Text(
                    _connecting
                        ? 'Connecting...'
                        : isConnected
                            ? 'Node connected'
                            : 'Node not connected  ·  tap to connect',
                    style: const TextStyle(color: Colors.grey, fontSize: 13),
                  ),
                ],
              ),
            ),
          ),
          const Divider(height: 1),
          if (_replyBody != null)
            Container(
              width: double.infinity,
              margin: const EdgeInsets.fromLTRB(16, 12, 16, 0),
              padding: const EdgeInsets.all(14),
              decoration: BoxDecoration(
                color: const Color(0xFFEFF4FF),
                borderRadius: BorderRadius.circular(12),
                border: Border.all(color: brandBlue.withValues(alpha: 0.4)),
              ),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Row(
                    children: [
                      const Icon(Icons.mark_email_read_outlined,
                          size: 18, color: brandBlue),
                      const SizedBox(width: 6),
                      Text('Reply from ${_replyFrom ?? ""}',
                          style: const TextStyle(
                              color: brandBlue,
                              fontWeight: FontWeight.w600,
                              fontSize: 13)),
                    ],
                  ),
                  const SizedBox(height: 6),
                  Text(_replyBody!, style: const TextStyle(fontSize: 15)),
                ],
              ),
            ),
          Expanded(
            child: Padding(
              padding: const EdgeInsets.all(24),
              child: Column(
                children: [
                  _bigButton(
                    icon: Icons.sms_outlined,
                    label: 'Send SMS',
                    onTap: () => Navigator.push(
                      context,
                      MaterialPageRoute(builder: (_) => const SmsScreen()),
                    ),
                  ),
                  const SizedBox(height: 16),
                  _bigButton(
                    icon: Icons.call_outlined,
                    label: 'Voice Call',
                    onTap: () => Navigator.push(
                      context,
                      MaterialPageRoute(builder: (_) => const VoiceScreen()),
                    ),
                  ),
                ],
              ),
            ),
          ),
          Padding(
            padding: const EdgeInsets.only(bottom: 20),
            child: GestureDetector(
              onTap: () {
                setState(() {
                  language =
                      language == 'Kinyarwanda' ? 'English' : 'Kinyarwanda';
                });
              },
              child: Text(
                'Kinyarwanda  ·  English   (current: $language)',
                style: const TextStyle(color: Colors.grey, fontSize: 13),
              ),
            ),
          ),
        ],
      ),
    );
  }

  Widget _bigButton(
      {required IconData icon,
      required String label,
      required VoidCallback onTap}) {
    return Expanded(
      child: InkWell(
        onTap: onTap,
        borderRadius: BorderRadius.circular(16),
        child: Container(
          width: double.infinity,
          decoration: BoxDecoration(
            border: Border.all(color: lightGray, width: 1.5),
            borderRadius: BorderRadius.circular(16),
          ),
          child: Column(
            mainAxisAlignment: MainAxisAlignment.center,
            children: [
              Icon(icon, size: 40, color: brandBlue),
              const SizedBox(height: 10),
              Text(label,
                  style: const TextStyle(
                      fontSize: 18, fontWeight: FontWeight.w600)),
            ],
          ),
        ),
      ),
    );
  }
}
