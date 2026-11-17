import 'package:flutter/material.dart';

import '../app/theme.dart';

// Boton de grabar: circulo rojo que pasa a cuadrado mientras graba
class RecordButton extends StatelessWidget {
  const RecordButton({super.key, required this.rec, required this.onTap, this.size = 88, this.enabled = true});
  final bool rec, enabled;
  final VoidCallback onTap;
  final double size;
  @override
  Widget build(BuildContext context) => GestureDetector(
        onTap: enabled ? onTap : null,
        child: Opacity(
          opacity: enabled ? 1 : 0.4,
          child: Container(
            width: size, height: size,
            decoration: BoxDecoration(shape: BoxShape.circle, border: Border.all(color: Colors.white, width: size / 22), color: Colors.black26),
            child: Center(child: AnimatedContainer(
              duration: const Duration(milliseconds: 180),
              width: rec ? size * 0.38 : size * 0.75, height: rec ? size * 0.38 : size * 0.75,
              decoration: BoxDecoration(color: kRec, borderRadius: BorderRadius.circular(rec ? size * 0.08 : size)),
            )),
          ),
        ),
      );
}
