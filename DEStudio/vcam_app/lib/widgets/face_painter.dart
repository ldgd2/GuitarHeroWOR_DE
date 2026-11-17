import 'dart:math' as math;

import 'package:flutter/material.dart';

import '../app/theme.dart';

// Cara detectada: contorno, labios, ojos, cejas e iris, con los puntos normalizados de MediaPipe (0..1 de la imagen).
// Anillos: 0 contorno, 1 labios, 2 boca por dentro, 3-4 ojos, 5-6 cejas (abiertas), 7-8 iris (un punto).
class FacePainter extends CustomPainter {
  FacePainter(this.pts, this.rings, {required this.ok, required this.mirror});
  final List<double> pts;
  final List<int> rings;
  final bool ok, mirror;

  @override
  void paint(Canvas c, Size size) {
    if (pts.length < 4 || rings.isEmpty) return;
    var minX = 1e9, maxX = -1e9, minY = 1e9, maxY = -1e9;
    for (var i = 0; i < pts.length; i += 2) {
      minX = math.min(minX, pts[i]); maxX = math.max(maxX, pts[i]);
      minY = math.min(minY, pts[i + 1]); maxY = math.max(maxY, pts[i + 1]);
    }
    final k = 0.8 * math.min(size.width / math.max(1e-6, maxX - minX), size.height / math.max(1e-6, maxY - minY));
    final cx = (minX + maxX) / 2, cy = (minY + maxY) / 2;
    Offset at(int i) => Offset(size.width / 2 + (mirror ? -1 : 1) * (pts[i * 2] - cx) * k, size.height / 2 + (pts[i * 2 + 1] - cy) * k);
    final line = Paint()..style = PaintingStyle.stroke..strokeWidth = 1.8..strokeJoin = StrokeJoin.round;
    var start = 0;
    for (var r = 0; r < rings.length; r++) {
      final n = rings[r];
      if (n == 0) continue;
      if (start + n > pts.length ~/ 2) break;
      final col = r == 0 ? (ok ? Colors.white30 : Colors.white12) : (ok ? kAccent : Colors.white24);
      if (n == 1) {
        c.drawCircle(at(start), 3, Paint()..color = col);
      } else {
        final path = Path()..moveTo(at(start).dx, at(start).dy);
        for (var i = 1; i < n; i++) { path.lineTo(at(start + i).dx, at(start + i).dy); }
        if (r != 5 && r != 6) path.close();
        c.drawPath(path, line..color = col);
      }
      start += n;
    }
  }

  @override
  bool shouldRepaint(FacePainter o) => true;
}
