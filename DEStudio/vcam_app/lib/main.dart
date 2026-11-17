// DE Studio en el movil: camara virtual y captura de interpretacion para el editor de canciones.
//
//   app/       tema
//   core/      cuaterniones, formato de tiempo
//   net/       conexion con el editor (EditorLink)
//   native/    puentes con el codigo Android (ARCore, CameraX + MediaPipe)
//   pages/     conexion, inicio (modos), camara virtual, captura
//   widgets/   piezas de interfaz y dibujos (cara, esqueleto, guias)
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import 'app/theme.dart';
import 'pages/connect_page.dart';

void main() {
  WidgetsFlutterBinding.ensureInitialized();
  SystemChrome.setPreferredOrientations([DeviceOrientation.landscapeLeft, DeviceOrientation.landscapeRight]);
  SystemChrome.setEnabledSystemUIMode(SystemUiMode.immersiveSticky);
  runApp(const VcamApp());
}

class VcamApp extends StatelessWidget {
  const VcamApp({super.key});
  @override
  Widget build(BuildContext context) => MaterialApp(
        title: 'DE Studio',
        debugShowCheckedModeBanner: false,
        theme: buildTheme(),
        home: const ConnectPage(),
      );
}
