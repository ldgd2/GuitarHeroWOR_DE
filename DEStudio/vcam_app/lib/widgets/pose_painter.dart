import 'package:flutter/material.dart';

import '../app/theme.dart';

// Esqueleto detectado (MediaPipe Pose, 33 puntos normalizados de la imagen: x, y, visibilidad)
const kPoseBones = [
  [11, 12], [11, 13], [13, 15], [12, 14], [14, 16], [15, 19], [16, 20], [11, 23], [12, 24], [23, 24],
  [23, 25], [25, 27], [27, 31], [24, 26], [26, 28], [28, 32], [7, 0], [8, 0],
];

class PosePainter extends CustomPainter {
  PosePainter(this.pts, {required this.ok, required this.mirror, this.aspect = 16 / 9, this.hands = const []});
  final List<double> pts;
  /// manos: 21 puntos x (x, y) por mano detectada
  final List<double> hands;
  final bool ok, mirror;
  final double aspect;

  @override
  void paint(Canvas c, Size size) {
    // la imagen de la camara encajada en el recuadro (sin deformar)
    var w = size.width, h = w / aspect;
    if (h > size.height) { h = size.height; w = h * aspect; }
    final ox = (size.width - w) / 2, oy = (size.height - h) / 2;
    c.drawRect(Rect.fromLTWH(ox, oy, w, h), Paint()..style = PaintingStyle.stroke..color = Colors.white12);
    if (pts.length < 33 * 3) return;
    Offset at(int i) => Offset(ox + (mirror ? 1 - pts[i * 3] : pts[i * 3]) * w, oy + pts[i * 3 + 1] * h);
    double vis(int i) => pts[i * 3 + 2];
    final p = Paint()..strokeWidth = 3..strokeCap = StrokeCap.round;
    for (final b in kPoseBones) {
      final v = (vis(b[0]) < vis(b[1]) ? vis(b[0]) : vis(b[1])).clamp(0.0, 1.0);
      p.color = (ok ? kAccent : Colors.white38).withValues(alpha: 0.25 + 0.75 * v);
      c.drawLine(at(b[0]), at(b[1]), p);
    }
    c.drawCircle(at(0), 5, Paint()..color = ok ? kAccent : Colors.white38);
    final hp = Paint()..strokeWidth = 2..strokeCap = StrokeCap.round..color = const Color(0xFF6EBEFF);
    for (var o = 0; o + 42 <= hands.length; o += 42) {
      Offset hpt(int i) => Offset(ox + (mirror ? 1 - hands[o + i * 2] : hands[o + i * 2]) * w, oy + hands[o + i * 2 + 1] * h);
      for (final ch in const [[0, 1, 2, 3, 4], [0, 5, 6, 7, 8], [5, 9, 10, 11, 12], [9, 13, 14, 15, 16], [13, 17, 18, 19, 20], [0, 17]]) {
        for (var k = 0; k + 1 < ch.length; k++) { c.drawLine(hpt(ch[k]), hpt(ch[k + 1]), hp); }
      }
    }
  }

  @override
  bool shouldRepaint(PosePainter o) => true;
}
