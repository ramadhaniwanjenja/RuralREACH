import 'package:flutter/material.dart';

class LegalPrivacyScreen extends StatefulWidget {
  const LegalPrivacyScreen({super.key});

  @override
  State<LegalPrivacyScreen> createState() => _LegalPrivacyScreenState();
}

class _LegalPrivacyScreenState extends State<LegalPrivacyScreen> {
  static const Color background = Color(0xFF08111F);
  static const Color surface = Color(0xFF111B2D);
  static const Color surfaceAlt = Color(0xFF162338);
  static const Color accent = Color(0xFF5B8CFF);
  static const Color textPrimary = Color(0xFFF4F7FB);
  static const Color textSecondary = Color(0xFFB8C2D6);

  bool _showPrivacyPolicy = true;

  @override
  Widget build(BuildContext context) {
    final sectionTitle = _showPrivacyPolicy ? 'Privacy Policy' : 'End User License Agreement';
    final sectionIcon = _showPrivacyPolicy ? Icons.privacy_tip_outlined : Icons.description_outlined;
    final sectionBody = _showPrivacyPolicy ? _privacyPolicyBody() : _eulaBody();

    return Scaffold(
      backgroundColor: background,
      appBar: AppBar(
        backgroundColor: background,
        foregroundColor: textPrimary,
        elevation: 0,
        title: const Text(
          'Legal & Privacy',
          style: TextStyle(fontWeight: FontWeight.w700),
        ),
      ),
      body: Container(
        decoration: const BoxDecoration(
          gradient: LinearGradient(
            begin: Alignment.topCenter,
            end: Alignment.bottomCenter,
            colors: [Color(0xFF08111F), Color(0xFF0D1728), Color(0xFF07101B)],
          ),
        ),
        child: SafeArea(
          child: SingleChildScrollView(
            padding: const EdgeInsets.fromLTRB(16, 8, 16, 24),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                Container(
                  padding: const EdgeInsets.all(20),
                  decoration: BoxDecoration(
                    gradient: const LinearGradient(
                      begin: Alignment.topLeft,
                      end: Alignment.bottomRight,
                      colors: [Color(0xFF12213A), Color(0xFF0D1A2D)],
                    ),
                    borderRadius: BorderRadius.circular(24),
                    border: Border.all(color: accent.withValues(alpha: 0.24)),
                  ),
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Row(
                        children: [
                          _heroIcon(Icons.shield_outlined),
                          const SizedBox(width: 12),
                          _heroIcon(Icons.privacy_tip_outlined),
                          const SizedBox(width: 12),
                          _heroIcon(Icons.workspace_premium_outlined),
                        ],
                      ),
                      const SizedBox(height: 18),
                      const Text(
                        'Academic compliance summary for RuralReach',
                        style: TextStyle(
                          color: textPrimary,
                          fontSize: 22,
                          fontWeight: FontWeight.w800,
                        ),
                      ),
                      const SizedBox(height: 8),
                      const Text(
                        'This module presents the privacy and licensing terms that govern encrypted SMS and voice communication in the RuralReach embedded system.',
                        style: TextStyle(
                          color: textSecondary,
                          height: 1.45,
                          fontSize: 14,
                        ),
                      ),
                    ],
                  ),
                ),
                const SizedBox(height: 16),
                Row(
                  children: [
                    Expanded(
                      child: _sectionButton(
                        icon: Icons.privacy_tip_outlined,
                        label: 'Privacy Policy',
                        active: _showPrivacyPolicy,
                        onTap: () => setState(() => _showPrivacyPolicy = true),
                      ),
                    ),
                    const SizedBox(width: 12),
                    Expanded(
                      child: _sectionButton(
                        icon: Icons.description_outlined,
                        label: 'End User License Agreement',
                        active: !_showPrivacyPolicy,
                        onTap: () => setState(() => _showPrivacyPolicy = false),
                      ),
                    ),
                  ],
                ),
                const SizedBox(height: 16),
                Container(
                  padding: const EdgeInsets.all(18),
                  decoration: BoxDecoration(
                    color: surface,
                    borderRadius: BorderRadius.circular(22),
                    border: Border.all(color: Colors.white.withValues(alpha: 0.08)),
                    boxShadow: [
                      BoxShadow(
                        color: Colors.black.withValues(alpha: 0.28),
                        blurRadius: 22,
                        offset: const Offset(0, 12),
                      ),
                    ],
                  ),
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Row(
                        children: [
                          Icon(sectionIcon, color: accent),
                          const SizedBox(width: 10),
                          Expanded(
                            child: Text(
                              sectionTitle,
                              style: const TextStyle(
                                color: textPrimary,
                                fontSize: 19,
                                fontWeight: FontWeight.w700,
                              ),
                            ),
                          ),
                        ],
                      ),
                      const SizedBox(height: 16),
                      sectionBody,
                    ],
                  ),
                ),
                const SizedBox(height: 16),
                Container(
                  padding: const EdgeInsets.all(16),
                  decoration: BoxDecoration(
                    color: surfaceAlt,
                    borderRadius: BorderRadius.circular(20),
                    border: Border.all(color: Colors.white.withValues(alpha: 0.08)),
                  ),
                  child: const Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text(
                        'System Notes',
                        style: TextStyle(
                          color: textPrimary,
                          fontSize: 16,
                          fontWeight: FontWeight.w700,
                        ),
                      ),
                      SizedBox(height: 10),
                      Text(
                        'RuralReach is built around encrypted LoRa relay, SMS delivery through the gateway, and voice communication support for constrained environments. This screen is presentation-only and does not alter network behavior.',
                        style: TextStyle(
                          color: textSecondary,
                          height: 1.45,
                        ),
                      ),
                    ],
                  ),
                ),
                const SizedBox(height: 18),
                SizedBox(
                  height: 52,
                  child: ElevatedButton.icon(
                    onPressed: () => Navigator.of(context).pop(),
                    icon: const Icon(Icons.arrow_back),
                    label: const Text('Back'),
                    style: ElevatedButton.styleFrom(
                      backgroundColor: accent,
                      foregroundColor: Colors.white,
                      shape: RoundedRectangleBorder(
                        borderRadius: BorderRadius.circular(16),
                      ),
                      textStyle: const TextStyle(fontWeight: FontWeight.w700),
                    ),
                  ),
                ),
              ],
            ),
          ),
        ),
      ),
    );
  }

  Widget _heroIcon(IconData icon) {
    return Container(
      width: 52,
      height: 52,
      decoration: BoxDecoration(
        color: Colors.white.withValues(alpha: 0.06),
        borderRadius: BorderRadius.circular(16),
        border: Border.all(color: Colors.white.withValues(alpha: 0.08)),
      ),
      child: Icon(icon, color: accent, size: 28),
    );
  }

  Widget _sectionButton({
    required IconData icon,
    required String label,
    required bool active,
    required VoidCallback onTap,
  }) {
    return SizedBox(
      height: 60,
      child: OutlinedButton.icon(
        onPressed: onTap,
        icon: Icon(icon, size: 18),
        label: Text(
          label,
          textAlign: TextAlign.center,
        ),
        style: OutlinedButton.styleFrom(
          backgroundColor: active ? accent.withValues(alpha: 0.18) : surface,
          foregroundColor: textPrimary,
          side: BorderSide(color: active ? accent : Colors.white.withValues(alpha: 0.12)),
          shape: RoundedRectangleBorder(
            borderRadius: BorderRadius.circular(16),
          ),
          padding: const EdgeInsets.symmetric(horizontal: 14),
          textStyle: const TextStyle(fontWeight: FontWeight.w700, fontSize: 12),
        ),
      ),
    );
  }

  Widget _sectionCard(String title, List<String> points) {
    return Container(
      margin: const EdgeInsets.only(bottom: 14),
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(
        color: surfaceAlt,
        borderRadius: BorderRadius.circular(18),
        border: Border.all(color: Colors.white.withValues(alpha: 0.06)),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(
            title,
            style: const TextStyle(
              color: textPrimary,
              fontSize: 16,
              fontWeight: FontWeight.w700,
            ),
          ),
          const SizedBox(height: 10),
          ...points.map(
            (point) => Padding(
              padding: const EdgeInsets.only(bottom: 8),
              child: Row(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  const Padding(
                    padding: EdgeInsets.only(top: 6),
                    child: Icon(Icons.circle, size: 7, color: accent),
                  ),
                  const SizedBox(width: 10),
                  Expanded(
                    child: Text(
                      point,
                      style: const TextStyle(
                        color: textSecondary,
                        height: 1.45,
                      ),
                    ),
                  ),
                ],
              ),
            ),
          ),
        ],
      ),
    );
  }

  Widget _privacyPolicyBody() {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        _sectionCard('Data Collection', [
          'RuralReach only processes information required to deliver SMS and voice communication, including destination phone number, encrypted payload, timestamps, delivery status, and basic diagnostic logs.',
        ]),
        _sectionCard('Data Protection', [
          'Message payloads are encrypted using AES before LoRa transmission and are only decrypted by the authorized gateway.',
        ]),
        _sectionCard('Data Retention', [
          'Delivery logs are retained only for troubleshooting and system reliability and may be deleted by the administrator.',
        ]),
        _sectionCard('User Rights', [
          'Users may request deletion of stored communication logs where applicable.',
        ]),
        _sectionCard('Security', [
          'Firmware validation, encrypted transmission, and access control help protect user information.',
        ]),
      ],
    );
  }

  Widget _eulaBody() {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        _sectionCard('Acceptable Use', [
          'RuralReach is intended solely for lawful communication and emergency connectivity.',
        ]),
        _sectionCard('Prohibited Use', [
          'Transmit illegal content.',
          'Attempt unauthorized network access.',
          'Modify firmware for malicious purposes.',
          'Interfere with public telecommunications.',
        ]),
        _sectionCard('Limitation of Liability', [
          'The developer is not liable for misuse, unauthorized modifications, or network outages.',
        ]),
        _sectionCard('User Responsibility', [
          'Users are responsible for complying with local telecommunications laws.',
        ]),
        _sectionCard('Software Updates', [
          'Firmware updates may introduce security improvements and bug fixes.',
        ]),
      ],
    );
  }
}
