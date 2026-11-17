import 'package:flutter/material.dart';

import '../app/theme.dart';

// Piezas pequenas comunes: piloto de estado, etiqueta, boton redondo con texto y aviso.
class StatusDot extends StatelessWidget {
  const StatusDot(this.color, {super.key});
  final Color color;
  @override
  Widget build(BuildContext context) => Container(width: 9, height: 9, decoration: BoxDecoration(color: color, shape: BoxShape.circle));
}

class InfoChip extends StatelessWidget {
  const InfoChip(this.text, this.icon, {super.key, this.color, this.onTap});
  final String text;
  final IconData icon;
  final Color? color;
  final VoidCallback? onTap;
  @override
  Widget build(BuildContext context) {
    final c = color ?? Colors.white;
    return GestureDetector(
      onTap: onTap,
      child: Container(
        padding: const EdgeInsets.symmetric(horizontal: 9, vertical: 5),
        decoration: BoxDecoration(color: Colors.black54, borderRadius: BorderRadius.circular(4), border: Border.all(color: c.withValues(alpha: 0.35))),
        child: Row(mainAxisSize: MainAxisSize.min, children: [
          Icon(icon, size: 15, color: c),
          const SizedBox(width: 5),
          Text(text, style: TextStyle(color: c, fontWeight: FontWeight.w600, fontSize: 13)),
        ]),
      ),
    );
  }
}

class RoundAction extends StatelessWidget {
  const RoundAction(this.icon, this.label, this.onTap, {super.key, this.on = false});
  final IconData icon;
  final String label;
  final VoidCallback? onTap;
  final bool on;
  @override
  Widget build(BuildContext context) => Column(mainAxisSize: MainAxisSize.min, children: [
        Material(
          color: on ? kAccent : Colors.white24, shape: const CircleBorder(),
          child: InkWell(customBorder: const CircleBorder(), onTap: onTap,
              child: Padding(padding: const EdgeInsets.all(11), child: Icon(icon, color: on ? Colors.black : Colors.white))),
        ),
        const SizedBox(height: 2),
        Text(label, style: const TextStyle(fontSize: 10, color: Colors.white70)),
      ]);
}

class WarnBox extends StatelessWidget {
  const WarnBox(this.text, {super.key});
  final String text;
  @override
  Widget build(BuildContext context) => Container(
        padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 7),
        decoration: BoxDecoration(color: const Color(0xE0221A0C), borderRadius: BorderRadius.circular(4), border: Border.all(color: kAccent.withValues(alpha: 0.4))),
        child: Text(text, style: const TextStyle(color: kAccent)),
      );
}
