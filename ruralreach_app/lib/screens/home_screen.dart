import 'package:flutter/material.dart';
import 'sms_screen.dart';
import 'voice_screen.dart';

class HomeScreen extends StatefulWidget {
  const HomeScreen({super.key});

  @override
  State<HomeScreen> createState() => _HomeScreenState();
}

class _HomeScreenState extends State<HomeScreen> {
  bool isConnected = false;
  String language = 'Kinyarwanda';

  static const Color brandBlue = Color(0xFF1D4ED8);
  static const Color lightGray = Color(0xFFD4D4D4);

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
          Padding(
            padding: const EdgeInsets.symmetric(vertical: 10, horizontal: 16),
            child: Row(
              children: [
                Text(
                  isConnected ? 'Node connected' : 'Node not connected',
                  style: const TextStyle(color: Colors.grey, fontSize: 13),
                ),
              ],
            ),
          ),
          const Divider(height: 1),
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
