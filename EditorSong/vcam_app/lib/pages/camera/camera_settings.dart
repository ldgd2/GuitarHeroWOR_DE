import 'package:flutter/material.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../../app/theme.dart';

// Ajustes de la camara virtual (se guardan en el telefono)
class CameraSettings {
  double moveSpeed = 1.5, rotGain = 1.0, scale = 1.0, stab = 0.35;
  int fps = 15, width = 640;
  bool grid = true, ar = true;

  Future<void> load() async {
    final p = await SharedPreferences.getInstance();
    moveSpeed = p.getDouble('moveSpeed') ?? moveSpeed;
    rotGain = p.getDouble('rotGain') ?? rotGain;
    scale = p.getDouble('scale') ?? scale;
    stab = p.getDouble('stab') ?? stab;
    fps = p.getInt('fps') ?? fps;
    width = p.getInt('width') ?? width;
    grid = p.getBool('grid') ?? grid;
    ar = p.getBool('ar') ?? ar;
  }

  Future<void> save() async {
    final p = await SharedPreferences.getInstance();
    await p.setDouble('moveSpeed', moveSpeed); await p.setDouble('rotGain', rotGain);
    await p.setDouble('scale', scale); await p.setDouble('stab', stab);
    await p.setInt('fps', fps); await p.setInt('width', width);
    await p.setBool('grid', grid); await p.setBool('ar', ar);
  }
}

/// Hoja de ajustes. onAr: activar/desactivar ARCore; onQuality: fps o resolucion cambiados; onChanged: redibujar.
void showCameraSettings(BuildContext context, CameraSettings s,
    {required ValueChanged<bool> onAr, required VoidCallback onQuality, required VoidCallback onChanged, required VoidCallback onDisconnect}) {
  showModalBottomSheet(
    context: context,
    isScrollControlled: true,
    backgroundColor: kPanel,
    builder: (_) => StatefulBuilder(builder: (ctx, set) {
      Widget slider(String label, String hint, double v, double min, double max, ValueChanged<double> f, {String Function(double)? fmtv}) => Padding(
            padding: const EdgeInsets.only(bottom: 4),
            child: Row(children: [
              SizedBox(width: 240, child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
                Text('$label: ${fmtv != null ? fmtv(v) : v.toStringAsFixed(2)}', style: const TextStyle(fontWeight: FontWeight.w600)),
                Text(hint, style: const TextStyle(fontSize: 11, color: kMuted)),
              ])),
              Expanded(child: Slider(value: v.clamp(min, max), min: min, max: max, onChanged: (x) { set(() => f(x)); onChanged(); }, onChangeEnd: (_) => s.save())),
            ]),
          );
      return SafeArea(
        child: Padding(
          padding: const EdgeInsets.fromLTRB(20, 12, 20, 12),
          child: SingleChildScrollView(child: Column(mainAxisSize: MainAxisSize.min, crossAxisAlignment: CrossAxisAlignment.stretch, children: [
            const Text('Ajustes de la camara', style: TextStyle(fontSize: 18, fontWeight: FontWeight.w700)),
            SwitchListTile(
              contentPadding: EdgeInsets.zero,
              title: const Text('Seguir mi posicion real (ARCore)'),
              subtitle: const Text('Caminar, agacharte o subir mueve la camara. Apagado: solo giroscopio y mando.'),
              value: s.ar,
              onChanged: (v) { set(() => s.ar = v); s.save(); onAr(v); },
            ),
            slider('Escala del movimiento', '1 m que te mueves = X m en el escenario', s.scale, 0.2, 5, (v) => s.scale = v, fmtv: (v) => '${v.toStringAsFixed(1)} m'),
            slider('Estabilizador', '0 = pulso de la mano tal cual; alto = steadicam', s.stab, 0, 0.9, (v) => s.stab = v),
            slider('Sensibilidad del giro', 'solo en modo giroscopio', s.rotGain, 0.2, 2, (v) => s.rotGain = v),
            slider('Velocidad del mando', 'metros por segundo', s.moveSpeed, 0.2, 6, (v) => s.moveSpeed = v),
            slider('Imagenes por segundo', 'mas = mas fluido, mas wifi', s.fps.toDouble(), 4, 30, (v) { s.fps = v.round(); onQuality(); }, fmtv: (v) => '${v.round()}'),
            slider('Resolucion de la imagen', 'ancho en pixeles', s.width.toDouble(), 320, 1280, (v) { s.width = (v / 160).round() * 160; onQuality(); },
                fmtv: (v) => '${(v / 160).round() * 160} px'),
            SwitchListTile(contentPadding: EdgeInsets.zero, title: const Text('Guias de tercios'), value: s.grid,
                onChanged: (v) { set(() => s.grid = v); onChanged(); s.save(); }),
            const SizedBox(height: 6),
            OutlinedButton.icon(
              onPressed: () { Navigator.of(ctx).pop(); onDisconnect(); },
              icon: const Icon(Icons.link_off), label: const Text('Desconectar'),
            ),
          ])),
        ),
      );
    }),
  );
}
