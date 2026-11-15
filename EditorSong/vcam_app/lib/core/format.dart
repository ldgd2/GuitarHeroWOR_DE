// m:ss.d de un tiempo de cancion en milisegundos
String fmtTime(int ms) {
  final neg = ms < 0;
  final s = ms.abs() / 1000.0;
  return '${neg ? '-' : ''}${s ~/ 60}:${(s % 60).toStringAsFixed(1).padLeft(4, '0')}';
}
