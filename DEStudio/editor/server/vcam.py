"""Camara virtual: puente WebSocket entre la app del movil (Flutter, carpeta vcam_app/) y el editor del navegador.

  movil  --(pose del giroscopio/acelerometro, zoom, mando, grabar)-->  editor
  editor --(fotogramas JPEG del monitor, estado de la grabacion)----->  movil

El editor HTTP sigue escuchando solo en 127.0.0.1; esto abre OTRO puerto (8766 por defecto) en la red local para
que el telefono llegue al PC. Solo acepta:
  /editor          desde 127.0.0.1 (el propio navegador del PC)
  /phone?pin=NNNN  desde la red local, con el PIN que muestra el editor (cambia en cada arranque)
y solo reenvia mensajes entre los dos: no lee ni escribe archivos.

WebSocket minimo (RFC 6455) con la biblioteca estandar: texto y binario sin fragmentar, ping/pong y cierre.
"""
from __future__ import annotations

import base64
import hashlib
import secrets
import socket
import struct
import threading
from urllib.parse import parse_qs, urlsplit

GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
PORT = 8766
PIN = f"{secrets.randbelow(10000):04d}"
MAX_FRAME = 4 * 1024 * 1024


class Peer:
    def __init__(self, sock: socket.socket, role: str, addr):
        self.sock, self.role, self.addr = sock, role, addr
        self.lock = threading.Lock()
        self.alive = True

    def send(self, data: bytes | str) -> bool:
        if isinstance(data, str):
            op, payload = 0x1, data.encode("utf-8")
        else:
            op, payload = 0x2, data
        n = len(payload)
        if n < 126:
            hdr = struct.pack("!BB", 0x80 | op, n)
        elif n < 65536:
            hdr = struct.pack("!BBH", 0x80 | op, 126, n)
        else:
            hdr = struct.pack("!BBQ", 0x80 | op, 127, n)
        try:
            with self.lock:
                self.sock.sendall(hdr + payload)
            return True
        except OSError:
            self.alive = False
            return False

    def close(self):
        self.alive = False
        try:
            self.sock.close()
        except OSError:
            pass


peers: dict[str, Peer | None] = {"editor": None, "phone": None}
peers_lock = threading.Lock()


def _recv_exact(sock: socket.socket, n: int) -> bytes:
    buf = b""
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise ConnectionError("cerrado")
        buf += chunk
    return buf


def _read_frame(sock: socket.socket):
    b0, b1 = _recv_exact(sock, 2)
    op, masked, n = b0 & 0x0F, b1 & 0x80, b1 & 0x7F
    if n == 126:
        n = struct.unpack("!H", _recv_exact(sock, 2))[0]
    elif n == 127:
        n = struct.unpack("!Q", _recv_exact(sock, 8))[0]
    if n > MAX_FRAME:
        raise ConnectionError("mensaje demasiado grande")
    mask = _recv_exact(sock, 4) if masked else b""
    data = _recv_exact(sock, n) if n else b""
    if masked:
        data = bytes(b ^ mask[i & 3] for i, b in enumerate(data))
    return op, data


def _other(role: str) -> Peer | None:
    with peers_lock:
        return peers["phone" if role == "editor" else "editor"]


def _status():
    with peers_lock:
        ed, ph = peers["editor"], peers["phone"]
    msg = '{"type":"link","editor":%s,"phone":%s}' % ("true" if ed else "false", "true" if ph else "false")
    for p in (ed, ph):
        if p:
            p.send(msg)


def _handshake(sock: socket.socket, addr):
    raw = b""
    while b"\r\n\r\n" not in raw:
        chunk = sock.recv(4096)
        if not chunk or len(raw) > 16384:
            return None
        raw += chunk
    head = raw.split(b"\r\n\r\n", 1)[0].decode("latin-1").split("\r\n")
    parts = head[0].split(" ")
    if len(parts) < 2:
        return None
    url = urlsplit(parts[1])
    hdr = {}
    for line in head[1:]:
        if ":" in line:
            k, v = line.split(":", 1)
            hdr[k.strip().lower()] = v.strip()
    key = hdr.get("sec-websocket-key")
    local = addr[0] in ("127.0.0.1", "::1")
    role = None
    if url.path == "/editor" and local:
        role = "editor"
    elif url.path == "/phone" and parse_qs(url.query).get("pin", [""])[0] == PIN:
        role = "phone"
    if not key or not role:
        sock.sendall(b"HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\nConnection: close\r\n\r\n")
        return None
    acc = base64.b64encode(hashlib.sha1((key + GUID).encode()).digest()).decode()
    sock.sendall(("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                  f"Sec-WebSocket-Accept: {acc}\r\n\r\n").encode())
    return role


def _client(sock: socket.socket, addr):
    sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    try:
        role = _handshake(sock, addr)
    except OSError:
        role = None
    if not role:
        sock.close()
        return
    me = Peer(sock, role, addr)
    with peers_lock:
        old = peers[role]
        peers[role] = me
    if old:
        old.close()
    print(f"[vcam] conectado: {role} desde {addr[0]}")
    _status()
    try:
        while me.alive:
            op, data = _read_frame(sock)
            if op == 0x8:
                break
            if op == 0x9:
                with me.lock:
                    sock.sendall(struct.pack("!BB", 0x8A, len(data)) + data)
                continue
            if op in (0x1, 0x2):
                other = _other(role)
                if other:
                    other.send(data.decode("utf-8", "replace") if op == 0x1 else data)
    except (OSError, ConnectionError, struct.error):
        pass
    finally:
        with peers_lock:
            if peers[role] is me:
                peers[role] = None
        me.close()
        print(f"[vcam] desconectado: {role}")
        _status()


def lan_ips() -> list[str]:
    ips = set()
    try:
        for info in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET):
            ips.add(info[4][0])
    except OSError:
        pass
    try:   # la interfaz con salida a la red (no envia nada)
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("10.255.255.255", 1))
        ips.add(s.getsockname()[0])
        s.close()
    except OSError:
        pass
    return sorted(i for i in ips if not i.startswith("127."))


_started = False


def start(port: int = PORT) -> bool:
    global _started, PORT
    if _started:
        return True
    PORT = port
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    if hasattr(socket, "SO_EXCLUSIVEADDRUSE"):   # Windows: que nadie mas pueda abrir el mismo puerto
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
    try:
        srv.bind(("0.0.0.0", port))
    except OSError as e:
        print(f"[vcam] no pude abrir el puerto {port}: {e}")
        return False
    srv.listen(4)
    _started = True

    def loop():
        while True:
            try:
                c, a = srv.accept()
            except OSError:
                break
            threading.Thread(target=_client, args=(c, a), daemon=True).start()

    threading.Thread(target=loop, daemon=True).start()
    print(f"[vcam] camara del movil: puerto {port}, PIN {PIN}, IP {', '.join(lan_ips()) or '?'}")
    return True


def info() -> dict:
    with peers_lock:
        ph = peers["phone"]
    return {"port": PORT, "pin": PIN, "ips": lan_ips(), "started": _started, "phone": bool(ph)}
