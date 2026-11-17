import 'package:flutter/material.dart';
import 'package:shared_preferences/shared_preferences.dart';
import 'package:web_socket_channel/web_socket_channel.dart';

import '../app/theme.dart';
import '../net/editor_link.dart';
import 'home_page.dart';

// Conexion con el editor del PC: IP, puerto y PIN (el PIN cambia cada vez que se abre el editor)
class ConnectPage extends StatefulWidget {
  const ConnectPage({super.key, this.message});
  final String? message;
  @override
  State<ConnectPage> createState() => _ConnectPageState();
}

class _ConnectPageState extends State<ConnectPage> {
  final ip = TextEditingController(), port = TextEditingController(text: '8766'), pin = TextEditingController();
  bool busy = false;
  String? error;

  @override
  void initState() {
    super.initState();
    error = widget.message;
    SharedPreferences.getInstance().then((p) {
      ip.text = p.getString('ip') ?? '';
      port.text = p.getString('port') ?? '8766';
      pin.text = p.getString('pin') ?? '';
    });
  }

  @override
  void dispose() {
    ip.dispose(); port.dispose(); pin.dispose();
    super.dispose();
  }

  Future<void> connect() async {
    setState(() { busy = true; error = null; });
    final uri = Uri.parse('ws://${ip.text.trim()}:${port.text.trim()}/phone?pin=${pin.text.trim()}');
    try {
      final ch = WebSocketChannel.connect(uri);
      await ch.ready.timeout(const Duration(seconds: 5));
      final p = await SharedPreferences.getInstance();
      await p.setString('ip', ip.text.trim());
      await p.setString('port', port.text.trim());
      await p.setString('pin', pin.text.trim());
      if (!mounted) return;
      Navigator.of(context).pushReplacement(MaterialPageRoute(builder: (_) => HomePage(link: EditorLink(ch))));
    } catch (e) {
      setState(() {
        busy = false;
        error = 'No se pudo conectar. Revisa la IP y el PIN (cambia cada vez que abres el editor) y que el PC y el '
            'telefono esten en la misma wifi.';
      });
    }
  }

  @override
  Widget build(BuildContext context) => Scaffold(
        body: SafeArea(
          child: Row(children: [
            Expanded(
              child: Container(
                color: kPanel,
                padding: const EdgeInsets.all(28),
                child: const Column(crossAxisAlignment: CrossAxisAlignment.start, mainAxisAlignment: MainAxisAlignment.center, children: [
                  Icon(Icons.videocam_outlined, size: 44, color: kAccent),
                  SizedBox(height: 12),
                  Text('DE Studio en el movil', style: TextStyle(fontSize: 26, fontWeight: FontWeight.w700)),
                  SizedBox(height: 8),
                  Text('Camara virtual y captura de cuerpo, cabeza y rostro para tus canciones.',
                      style: TextStyle(color: kMuted, fontSize: 15)),
                  SizedBox(height: 16),
                  _Step(n: 1, t: 'Abre DE Studio en el PC.'),
                  _Step(n: 2, t: 'Pestana Camaras > Camara del movil: alli estan la IP y el PIN.'),
                  _Step(n: 3, t: 'El PC y el telefono, en la misma wifi.'),
                ]),
              ),
            ),
            Expanded(
              child: Center(
                child: SingleChildScrollView(
                  padding: const EdgeInsets.all(28),
                  child: Column(mainAxisSize: MainAxisSize.min, crossAxisAlignment: CrossAxisAlignment.stretch, children: [
                    TextField(controller: ip, decoration: const InputDecoration(labelText: 'IP del PC', hintText: '192.168.1.20', prefixIcon: Icon(Icons.computer)), keyboardType: TextInputType.url),
                    const SizedBox(height: 12),
                    Row(children: [
                      Expanded(child: TextField(controller: port, decoration: const InputDecoration(labelText: 'Puerto', prefixIcon: Icon(Icons.numbers)), keyboardType: TextInputType.number)),
                      const SizedBox(width: 12),
                      Expanded(child: TextField(controller: pin, decoration: const InputDecoration(labelText: 'PIN', prefixIcon: Icon(Icons.lock_outline)), keyboardType: TextInputType.number)),
                    ]),
                    const SizedBox(height: 20),
                    FilledButton.icon(
                      style: FilledButton.styleFrom(padding: const EdgeInsets.symmetric(vertical: 16)),
                      onPressed: busy ? null : connect,
                      icon: busy ? const SizedBox(width: 18, height: 18, child: CircularProgressIndicator(strokeWidth: 2)) : const Icon(Icons.link),
                      label: Text(busy ? 'Conectando...' : 'Conectar'),
                    ),
                    if (error != null) Padding(padding: const EdgeInsets.only(top: 12), child: Text(error!, style: const TextStyle(color: kRec))),
                  ]),
                ),
              ),
            ),
          ]),
        ),
      );
}

class _Step extends StatelessWidget {
  const _Step({required this.n, required this.t});
  final int n;
  final String t;
  @override
  Widget build(BuildContext context) => Padding(
        padding: const EdgeInsets.only(top: 6),
        child: Row(children: [
          CircleAvatar(radius: 11, backgroundColor: kAccent, child: Text('$n', style: const TextStyle(fontSize: 12, color: Colors.black, fontWeight: FontWeight.bold))),
          const SizedBox(width: 10),
          Expanded(child: Text(t, style: const TextStyle(color: kMuted))),
        ]),
      );
}
