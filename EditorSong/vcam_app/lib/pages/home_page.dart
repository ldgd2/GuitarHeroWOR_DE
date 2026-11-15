import 'package:flutter/material.dart';
import 'package:wakelock_plus/wakelock_plus.dart';

import '../app/theme.dart';
import '../net/editor_link.dart';
import 'camera/camera_page.dart';
import 'capture/capture_mode.dart';
import 'capture/capture_page.dart';
import 'connect_page.dart';

// Pantalla principal: barra lateral con los modos. Solo uno usa la camara a la vez.
//   Camara          camara virtual del editor (ARCore + giroscopio)
//   Cuerpo          animacion de cuerpo entero (sin cara)
//   Cabeza y rostro giro de la cabeza + gestos de la cara
//   Rostro          solo gestos, con la cara mas grande en la imagen (mas detalle)
class HomePage extends StatefulWidget {
  const HomePage({super.key, required this.link});
  final EditorLink link;
  @override
  State<HomePage> createState() => _HomePageState();
}

class _HomePageState extends State<HomePage> {
  int index = 0;

  @override
  void initState() {
    super.initState();
    WakelockPlus.enable();
    widget.link.addListener(onLink);
  }

  @override
  void dispose() {
    widget.link.removeListener(onLink);
    WakelockPlus.disable();
    super.dispose();
  }

  void onLink() {
    if (!widget.link.closed || !mounted) return;
    widget.link.removeListener(onLink);
    Navigator.of(context).pushReplacement(MaterialPageRoute(builder: (_) => const ConnectPage(message: 'Se perdio la conexion con el editor.')));
  }

  @override
  Widget build(BuildContext context) {
    final link = widget.link;
    final page = switch (index) {
      0 => CameraPage(key: const ValueKey('cam'), link: link),
      1 => CapturePage(key: const ValueKey('body'), link: link, mode: CaptureMode.body),
      2 => CapturePage(key: const ValueKey('head'), link: link, mode: CaptureMode.head),
      _ => CapturePage(key: const ValueKey('face'), link: link, mode: CaptureMode.face),
    };
    return Scaffold(
      backgroundColor: Colors.black,
      body: Row(children: [
        NavigationRail(
          backgroundColor: kPanel,
          selectedIndex: index,
          labelType: NavigationRailLabelType.all,
          indicatorColor: kAccent.withValues(alpha: 0.22),
          selectedIconTheme: const IconThemeData(color: kAccent),
          onDestinationSelected: link.rec || link.capRec ? null : (i) => setState(() => index = i),
          destinations: const [
            NavigationRailDestination(icon: Icon(Icons.videocam_outlined), selectedIcon: Icon(Icons.videocam), label: Text('Camara')),
            NavigationRailDestination(icon: Icon(Icons.accessibility_new_outlined), selectedIcon: Icon(Icons.accessibility_new), label: Text('Cuerpo')),
            NavigationRailDestination(icon: Icon(Icons.face_outlined), selectedIcon: Icon(Icons.face), label: Text('Cabeza\ny rostro', textAlign: TextAlign.center)),
            NavigationRailDestination(icon: Icon(Icons.sentiment_satisfied_outlined), selectedIcon: Icon(Icons.sentiment_satisfied), label: Text('Rostro')),
          ],
        ),
        const VerticalDivider(width: 1),
        Expanded(child: page),
      ]),
    );
  }
}
