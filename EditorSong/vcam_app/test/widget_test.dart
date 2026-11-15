import 'dart:math' as math;
import 'package:flutter_test/flutter_test.dart';
import 'package:vcam_app/core/quat.dart';

void main() {
  test('girar 90 grados en Y lleva -Z a -X', () {
    final v = Quat.fromRotVec(0, math.pi / 2, 0).rotate(0, 0, -1);
    expect(v[0], closeTo(-1, 1e-6));
    expect(v[2], closeTo(0, 1e-6));
  });
  test('rumbo y nivelado: avanzar 1 m mirando a la izquierda queda como "adelante" (-Z) en el marco de partida', () {
    final base = Quat.fromRotVec(0, math.pi / 2, 0).mul(Quat.fromRotVec(-0.4, 0, 0));   // a la izquierda y mirando abajo
    final d = base.rotate(0, 0, -1);
    final lev = Quat.yaw(-base.heading()).rotate(d[0], 0, d[2]);
    expect(lev[0], closeTo(0, 1e-6));
    expect(lev[2], lessThan(0));
  });
  test('slerp a mitad', () {
    final a = Quat(), b = Quat.fromRotVec(0, 1, 0);
    final m = a.slerpTo(b, 0.5);
    final r = Quat.fromRotVec(0, 0.5, 0);
    expect(m.y, closeTo(r.y, 1e-6));
  });
}
