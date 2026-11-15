"""Cliente del puente de desarrollo de re-wor (el juego tiene que estar abierto).

    python tools/qb.py debugmenu
    python tools/qb.py run ui_event event=menu_change data={state=UIstate_debug}
    python tools/qb.py get controller_unplugged_frame_count
    python tools/qb.py set controller_unplugged_frame_count 60
    python tools/qb.py            (sin argumentos: modo interactivo)

Lee el puerto y la clave de dev_bridge.json, que el juego escribe en su carpeta de trabajo al arrancar
(se borra al cerrar). Solo escucha en 127.0.0.1.
"""
import json
import socket
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def find_info():
    """el dev_bridge.json mas reciente: carpeta de trabajo del juego (junto al .exe o la raiz)"""
    found = [p for p in [ROOT / 'dev_bridge.json', *ROOT.glob('out/build/*/dev_bridge.json')] if p.exists()]
    if not found:
        raise FileNotFoundError('dev_bridge.json')
    return max(found, key=lambda p: p.stat().st_mtime)


class Bridge:
    def __init__(self, info=None):
        cfg = json.loads(Path(info or find_info()).read_text(encoding='utf-8'))
        self.sock = socket.create_connection(('127.0.0.1', int(cfg['port'])), timeout=8)
        self.buf = b''
        self.line()                                   # saludo
        if self.send('auth ' + cfg['token']) != 'ok':
            raise RuntimeError('clave rechazada (dev_bridge.json de otra sesion?)')

    def line(self):
        while b'\n' not in self.buf:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise ConnectionError('el juego cerro la conexion')
            self.buf += chunk
        out, self.buf = self.buf.split(b'\n', 1)
        return out.decode('utf-8', 'replace')

    def send(self, cmd):
        self.sock.sendall(cmd.encode('utf-8') + b'\n')
        return self.line()

    def close(self):
        try:
            self.sock.sendall(b'quit\n')
        finally:
            self.sock.close()


def main(argv):
    try:
        b = Bridge()
    except FileNotFoundError:
        print('No hay dev_bridge.json: el juego no esta abierto o [dev] bridge_port = 0')
        return 1
    try:
        if argv:
            r = b.send(' '.join(argv))
            print(r)
            return 0 if r.startswith('ok') else 2
        while True:
            try:
                cmd = input('qb> ').strip()
            except EOFError:
                break
            if cmd in ('', 'quit', 'exit'):
                break
            print(b.send(cmd))
    finally:
        b.close()
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
