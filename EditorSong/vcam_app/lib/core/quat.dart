import 'dart:math' as math;

// Cuaternion minimo (x, y, z, w)
class Quat {
  double x, y, z, w;
  Quat([this.x = 0, this.y = 0, this.z = 0, this.w = 1]);
  Quat.list(List l) : x = (l[0] as num).toDouble(), y = (l[1] as num).toDouble(), z = (l[2] as num).toDouble(), w = (l[3] as num).toDouble();
  Quat copy() => Quat(x, y, z, w);
  Quat mul(Quat b) => Quat(
        w * b.x + x * b.w + y * b.z - z * b.y,
        w * b.y - x * b.z + y * b.w + z * b.x,
        w * b.z + x * b.y - y * b.x + z * b.w,
        w * b.w - x * b.x - y * b.y - z * b.z,
      );
  Quat inv() => Quat(-x, -y, -z, w);
  void normalize() {
    final n = math.sqrt(x * x + y * y + z * z + w * w);
    if (n > 0) { x /= n; y /= n; z /= n; w /= n; }
  }
  static Quat fromRotVec(double ax, double ay, double az) {
    final a = math.sqrt(ax * ax + ay * ay + az * az);
    if (a < 1e-9) return Quat();
    final s = math.sin(a / 2) / a;
    return Quat(ax * s, ay * s, az * s, math.cos(a / 2));
  }
  static Quat yaw(double a) => Quat(0, math.sin(a / 2), 0, math.cos(a / 2));
  List<double> rotate(double vx, double vy, double vz) {   // q * v * q^-1
    final tx = 2 * (y * vz - z * vy), ty = 2 * (z * vx - x * vz), tz = 2 * (x * vy - y * vx);
    return [vx + w * tx + (y * tz - z * ty), vy + w * ty + (z * tx - x * tz), vz + w * tz + (x * ty - y * tx)];
  }
  // rumbo (giro alrededor de Y) de hacia donde mira (-Z)
  double heading() {
    final f = rotate(0, 0, -1);
    return math.atan2(-f[0], -f[2]);
  }
  Quat slerpTo(Quat b, double t) {
    var d = x * b.x + y * b.y + z * b.z + w * b.w;
    final s = d < 0 ? -1.0 : 1.0;
    d = d.abs();
    if (d > 0.9995) {
      final r = Quat(x + (s * b.x - x) * t, y + (s * b.y - y) * t, z + (s * b.z - z) * t, w + (s * b.w - w) * t);
      r.normalize();
      return r;
    }
    final th = math.acos(d), sn = math.sin(th), a = math.sin((1 - t) * th) / sn, c = s * math.sin(t * th) / sn;
    return Quat(x * a + b.x * c, y * a + b.y * c, z * a + b.z * c, w * a + b.w * c);
  }
}
