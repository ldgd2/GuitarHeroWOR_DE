import 'package:flutter/material.dart';

// guias de tercios y centro
class ThirdsGuides extends CustomPainter {
  @override
  void paint(Canvas c, Size s) {
    final p = Paint()..color = const Color(0x55FFFFFF)..strokeWidth = 1;
    for (var i = 1; i < 3; i++) {
      c.drawLine(Offset(s.width * i / 3, 0), Offset(s.width * i / 3, s.height), p);
      c.drawLine(Offset(0, s.height * i / 3), Offset(s.width, s.height * i / 3), p);
    }
    final cx = s.width / 2, cy = s.height / 2;
    final pc = Paint()..color = const Color(0x99FFFFFF)..strokeWidth = 1.5;
    c.drawLine(Offset(cx - 10, cy), Offset(cx + 10, cy), pc);
    c.drawLine(Offset(cx, cy - 10), Offset(cx, cy + 10), pc);
  }
  @override
  bool shouldRepaint(covariant CustomPainter old) => false;
}

