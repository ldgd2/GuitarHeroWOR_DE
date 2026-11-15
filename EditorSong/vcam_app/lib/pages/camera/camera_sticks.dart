import 'package:flutter/material.dart';

// Mando virtual: x = lateral, y = adelante (arriba en pantalla = avanzar = -z)
class Stick extends StatefulWidget {
  const Stick({super.key, required this.onChange});
  final ValueChanged<Offset> onChange;
  @override
  State<Stick> createState() => StickState();
}

class StickState extends State<Stick> {
  Offset k = Offset.zero;
  static const r = 62.0;
  void upd(Offset local) {
    var d = local - const Offset(r, r);
    if (d.distance > r) d = d / d.distance * r;
    setState(() => k = d);
    final n = d / r;
    widget.onChange(n * n.distance);   // respuesta suave cerca del centro
  }
  @override
  Widget build(BuildContext context) => GestureDetector(
        onPanStart: (d) => upd(d.localPosition),
        onPanUpdate: (d) => upd(d.localPosition),
        onPanEnd: (_) { setState(() => k = Offset.zero); widget.onChange(Offset.zero); },
        child: Container(
          width: r * 2, height: r * 2,
          decoration: BoxDecoration(shape: BoxShape.circle, color: Colors.black38, border: Border.all(color: Colors.white30, width: 2)),
          child: Stack(alignment: Alignment.center, children: [
            const Icon(Icons.open_with, color: Colors.white24, size: 40),
            Transform.translate(offset: k, child: Container(width: 50, height: 50,
                decoration: BoxDecoration(shape: BoxShape.circle, color: Colors.white.withValues(alpha: 0.7), boxShadow: const [BoxShadow(color: Colors.black54, blurRadius: 6)]))),
          ]),
        ),
      );
}

// Deslizador de altura que vuelve al centro: arriba = subir
class Lift extends StatefulWidget {
  const Lift({super.key, required this.onChange});
  final ValueChanged<double> onChange;
  @override
  State<Lift> createState() => LiftState();
}

class LiftState extends State<Lift> {
  double v = 0;
  static const h = 124.0;
  void upd(double y) {
    final x = ((h / 2 - y) / (h / 2)).clamp(-1.0, 1.0);
    setState(() => v = x);
    widget.onChange(x * x.abs());
  }
  @override
  Widget build(BuildContext context) => GestureDetector(
        onVerticalDragStart: (d) => upd(d.localPosition.dy),
        onVerticalDragUpdate: (d) => upd(d.localPosition.dy),
        onVerticalDragEnd: (_) { setState(() => v = 0); widget.onChange(0); },
        child: Container(
          width: 46, height: h,
          decoration: BoxDecoration(color: Colors.black38, borderRadius: BorderRadius.circular(23), border: Border.all(color: Colors.white30, width: 2)),
          child: Stack(children: [
            const Center(child: Icon(Icons.height, color: Colors.white24)),
            Positioned(left: 3, top: h / 2 - 19 - v * (h / 2 - 21), child: Container(width: 36, height: 36,
                decoration: BoxDecoration(shape: BoxShape.circle, color: Colors.white.withValues(alpha: 0.7)))),
          ]),
        ),
      );
}
