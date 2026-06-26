import 'package:flutter/material.dart';
import 'screens/home_screen.dart';

void main() {
  runApp(const RuralReachApp());
}

class RuralReachApp extends StatelessWidget {
  const RuralReachApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'RuralReach',
      debugShowCheckedModeBanner: false,
      theme: ThemeData(
        colorScheme: ColorScheme.fromSeed(seedColor: const Color(0xFF1D4ED8)),
        useMaterial3: true,
        scaffoldBackgroundColor: Colors.white,
      ),
      home: const HomeScreen(),
    );
  }
}
