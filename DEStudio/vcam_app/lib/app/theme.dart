import 'package:flutter/material.dart';

// Mismo lenguaje visual que el editor: grafito, un solo acento ambar y rojo solo para grabar
const kAccent = Color(0xFFE3A043);
const kRec = Color(0xFFE5484D);
const kOk = Color(0xFF3FB67A);
const kBg = Color(0xFF0D0F12);
const kPanel = Color(0xFF14171B);
const kPanel2 = Color(0xFF1A1E23);
const kLine = Color(0xFF252A31);
const kMuted = Color(0xFF9097A2);

ThemeData buildTheme() => ThemeData(
      useMaterial3: true,
      brightness: Brightness.dark,
      scaffoldBackgroundColor: kBg,
      colorScheme: ColorScheme.fromSeed(seedColor: kAccent, brightness: Brightness.dark, primary: kAccent, surface: kPanel),
      dividerColor: kLine,
      sliderTheme: const SliderThemeData(activeTrackColor: kAccent, thumbColor: kAccent, inactiveTrackColor: kLine),
    );
