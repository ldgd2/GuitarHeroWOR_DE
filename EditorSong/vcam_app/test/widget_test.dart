import 'package:flutter_test/flutter_test.dart';
import 'package:vcam_app/main.dart';

void main() {
  test('cuaternion: girar 90 grados en Y lleva -Z a -X', () {
    final q = Quat.fromRotVec(0, 1.5707963, 0);
    final v = q.rotate(0, 0, -1);
    expect(v[0], closeTo(-1, 1e-6));
    expect(v[2], closeTo(0, 1e-6));
  });
}
