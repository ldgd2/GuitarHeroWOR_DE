"""qbc - compilador de scripts QB para Guitar Hero: Warriors of Rock (Xbox 360).

Convierte un archivo de texto (.q) en un .qb.xen que el motor registra con LoadQB
(re-wor lo carga al arrancar desde qb/). Solo genera codigo propio: no copia nada del juego.

Sintaxis (parecida a la de Neversoft):

    // comentario
    int    re_gfx_scale = 1          global entera
    float  re_gfx_bloom = 0.5        global real
    string re_gfx_note = "texto"     global de texto
    name   re_gfx_mode = windowed    global con una clave
    hook   ui_create_options         envolver un script del juego: el original pasa a llamarse
                                     re_orig_ui_create_options y este archivo define el nuevo

    script re_hola modo=1 texto="hola"   parametros por defecto (opcionales)
        Change re_gfx_scale = 2       llamada: nombre y parametros
        x = 3                         variable local; se lee como <x>
        if (<x> = 3) ... elseif ... else ... endif
        begin ... break ... repeat 4
        switch <x>  case 1 ... case 2 ... default ... endswitch
        id :SE_SetProps alpha = 0     metodo de un objeto
        return y = <x>
    endscript

Valores: 12, -3, 1.5, "texto", w"texto ancho", qs(0x1234abcd), nombre, $global,
<local>, <...> (todos los parametros), {estructura}, [array], (expresion).
Un '-' pegado a un numero es el signo (-1); para restar, con espacio: <x> - 1.
Operadores: = (asignar y comparar), != < > <= >= + - * / . , : not and or.
Una sentencia sigue en la linea siguiente si quedan llaves, corchetes o parentesis abiertos.

Uso: python tools/qbc.py entrada.q salida.qb.xen
"""
from __future__ import annotations

import re
import struct
import sys
from pathlib import Path


def qbkey(name: str) -> int:
    """clave QB: CRC32 sin inversion final, en minusculas y con '\\'"""
    c = 0xFFFFFFFF
    for ch in name.lower().replace('/', '\\').encode('latin1'):
        c ^= ch
        for _ in range(8):
            c = (c >> 1) ^ (0xEDB88320 & -(c & 1))
    return c


# ---------------------------------------------------------------------------- lexico
TOKEN = re.compile(r'''
    (?P<ws>[ \t\r]+)
  | (?P<comment>//[^\n]*)
  | (?P<nl>\n)
  | (?P<wstr>w"(?:[^"\\]|\\.)*")
  | (?P<str>"(?:[^"\\]|\\.)*")
  | (?P<qs>qs\(\s*(?:0x[0-9a-fA-F]+)\s*\))
  | (?P<allargs><\.\.\.>)
  | (?P<local><[A-Za-z_][A-Za-z0-9_]*>)
  | (?P<hex>0x[0-9a-fA-F]+)
  | (?P<float>-?\d+\.\d*|-?\.\d+)
  | (?P<int>-?\d+)
  | (?P<glob>\$[A-Za-z_][A-Za-z0-9_]*)
  | (?P<path>[A-Za-z]:/[A-Za-z0-9_/\.]+)
  | (?P<name>[A-Za-z_][A-Za-z0-9_/\\.]*[A-Za-z0-9_]|[A-Za-z_])
  | (?P<op>!=|<=|>=|[{}\[\]()=<>+\-*/.,:])
''', re.X)


class QbcError(Exception):
    pass


def lex(src: str):
    out, line, pos = [], 1, 0
    while pos < len(src):
        m = TOKEN.match(src, pos)
        if not m:
            raise QbcError(f'linea {line}: caracter inesperado {src[pos]!r}')
        kind, text = m.lastgroup, m.group()
        pos = m.end()
        if kind == 'nl':
            out.append(('nl', text, line))
            line += 1
        elif kind not in ('ws', 'comment'):
            out.append((kind, text, line))
    return fold_vectors(out)


def fold_vectors(toks):
    """(x,y) y (x,y,z) con numeros -> un solo token par/vector (0x1F / 0x1E). Sin esto la coma sale como 0x09
    y el analizador de expresiones del juego (sub_823E6600) se queda en un bucle infinito."""
    out, i = [], 0
    num = ('int', 'float')
    while i < len(toks):
        t = toks[i]
        if t[0] == 'op' and t[1] == '(':
            for n in (3, 2):
                j = i + 1
                vals = []
                ok = True
                for k in range(n):
                    if j < len(toks) and toks[j][0] in num:
                        vals.append(float(toks[j][1]))
                        j += 1
                    else:
                        ok = False
                        break
                    want = ',' if k < n - 1 else ')'
                    if j < len(toks) and toks[j][0] == 'op' and toks[j][1] == want:
                        j += 1
                    else:
                        ok = False
                        break
                if ok:
                    out.append(('pair' if n == 2 else 'vec', tuple(vals), t[2]))
                    i = j
                    break
            else:
                out.append(t)
                i += 1
            continue
        out.append(t)
        i += 1
    return out


def statements(tokens):
    """agrupa los tokens en sentencias (una por linea, salvo parentesis abiertos)"""
    cur, depth = [], 0
    for t in tokens:
        if t[0] == 'nl':
            if depth > 0:
                continue
            if cur:
                yield cur
                cur = []
            continue
        if t[0] == 'op' and t[1] in '{[(':
            depth += 1
        elif t[0] == 'op' and t[1] in '}])':
            depth -= 1
        cur.append(t)
    if cur:
        yield cur


# ---------------------------------------------------------------------------- bytecode
OPS = {'{': 0x03, '}': 0x04, '[': 0x05, ']': 0x06, '=': 0x07, '.': 0x08, ',': 0x09,
       '-': 0x0A, '+': 0x0B, '/': 0x0C, '*': 0x0D, '(': 0x0E, ')': 0x0F,
       '<': 0x12, '<=': 0x13, '>': 0x14, '>=': 0x15, ':': 0x42, '!=': 0x4D}
WORDS = {'not': 0x39, 'and': 0x33, 'or': 0x32}


def emit_value(out: bytearray, t, line):
    kind, text = t[0], t[1]
    if kind == 'int':
        out += b'\x17' + struct.pack('<i', int(text))
    elif kind == 'hex':
        out += b'\x17' + struct.pack('<I', int(text, 16))
    elif kind == 'float':
        out += b'\x1A' + struct.pack('<f', float(text))
    elif kind == 'str':
        s = bytes(text[1:-1], 'latin1').decode('unicode_escape').encode('latin1') + b'\0'
        out += b'\x1B' + struct.pack('<I', len(s)) + s
    elif kind == 'wstr':
        s = bytes(text[2:-1], 'latin1').decode('unicode_escape').encode('utf-16-be') + b'\0\0'
        out += b'\x4C' + struct.pack('<I', len(s)) + s
    elif kind == 'qs':
        out += b'\x4E' + struct.pack('<I', int(text[3:-1].strip(), 16))
    elif kind == 'pair':
        out += b'' + struct.pack('<ff', *text)
    elif kind == 'vec':
        out += b'' + struct.pack('<fff', *text)
    elif kind == 'allargs':
        out += b'\x2C'
    elif kind == 'local':
        out += b'\x2D\x16' + struct.pack('<I', qbkey(text[1:-1]))
    elif kind == 'glob':
        out += b'\x4B\x16' + struct.pack('<I', qbkey(text[1:]))
    elif kind == 'path':   # ruta de un recurso (c:/gh6_burn/...): su clave, como un nombre
        out += b'' + struct.pack('<I', qbkey(text))
    elif kind == 'name':
        if text in WORDS:
            out.append(WORDS[text])
        else:
            out += b'\x16' + struct.pack('<I', qbkey(text))
    elif kind == 'op':
        out.append(OPS[text])
    else:
        raise QbcError(f'linea {line}: no se esperaba {text!r}')


def compile_script(body: list, defaults: list = ()) -> bytes:
    """body: sentencias entre 'script nombre' y 'endscript'; defaults: parametros por defecto"""
    out = bytearray()
    for t in defaults:   # van antes de la primera linea, sin 0x01
        emit_value(out, t, t[2])
    blocks = []      # pila de if / switch abiertos
    patches = []     # (posicion del campo, destino(), ancho, origen)

    def fix16(field_pos, origin):
        def apply(target):
            off = target - origin
            if not 0 <= off < 0x10000:
                raise QbcError('salto fuera de rango')
            struct.pack_into('<H', out, field_pos, off)
        return apply

    for st in body:
        word = st[0][1] if st[0][0] == 'name' else None
        line = st[0][2]
        rest = st[1:]
        if word == 'if':
            out += b'\x01'
            p = len(out)
            out += b'\x47\0\0'
            blocks.append({'kind': 'if', 'pending': fix16(p + 1, p + 1), 'ends': []})
            for t in rest:
                emit_value(out, t, line)
        elif word == 'elseif':
            b = blocks[-1] if blocks and blocks[-1]['kind'] == 'if' else None
            if not b:
                raise QbcError(f'linea {line}: elseif sin if')
            out += b'\x01'
            p = len(out)
            b['pending'](p)                      # la rama anterior salta a este elseif
            out += b'\x27\0\0\0\0'
            b['pending'] = fix16(p + 1, p + 1)
            b['ends'].append(fix16(p + 3, p + 3))
            for t in rest:
                emit_value(out, t, line)
        elif word == 'else':
            b = blocks[-1] if blocks and blocks[-1]['kind'] == 'if' else None
            if not b:
                raise QbcError(f'linea {line}: else sin if')
            out += b'\x01'
            p = len(out)
            out += b'\x48\0\0'
            b['pending'](p + 3)                  # la rama anterior salta al cuerpo del else
            b['pending'] = None
            b['ends'].append(fix16(p + 1, p + 1))
        elif word == 'endif':
            b = blocks.pop() if blocks and blocks[-1]['kind'] == 'if' else None
            if not b:
                raise QbcError(f'linea {line}: endif sin if')
            out += b'\x01'
            p = len(out)
            out += b'\x28'
            if b['pending']:
                b['pending'](p + 1)
            for e in b['ends']:
                e(p + 1)
        elif word == 'switch':
            out += b'\x01\x3C'
            for t in rest:
                emit_value(out, t, line)
            blocks.append({'kind': 'switch', 'next': None, 'breaks': [], 'started': False})
        elif word in ('case', 'default'):
            b = blocks[-1] if blocks and blocks[-1]['kind'] == 'switch' else None
            if not b:
                raise QbcError(f'linea {line}: {word} fuera de switch')
            out += b'\x01'
            if b['started']:                     # fin del caso anterior: salta tras endswitch
                p = len(out)
                out += b'\x49\0\0'
                b['breaks'].append(fix16(p + 1, p + 1))
            p = len(out)
            if b['next']:
                b['next'](p)                     # la comprobacion anterior salta aqui
            out += (b'\x3E' if word == 'case' else b'\x3F') + b'\x49\0\0'
            b['next'] = fix16(p + 2, p + 2)
            b['started'] = True
            for t in rest:
                emit_value(out, t, line)
        elif word == 'endswitch':
            b = blocks.pop() if blocks and blocks[-1]['kind'] == 'switch' else None
            if not b:
                raise QbcError(f'linea {line}: endswitch sin switch')
            out += b'\x01'
            p = len(out)
            out += b'\x3D'
            if b['next']:
                b['next'](p)
            for e in b['breaks']:
                e(p + 1)
        elif word in ('begin', 'repeat', 'break', 'return'):
            out += b'\x01' + {'begin': b'\x20', 'repeat': b'\x21', 'break': b'\x22', 'return': b'\x29'}[word]
            for t in rest:
                emit_value(out, t, line)
        else:
            out += b'\x01'
            for t in st:
                emit_value(out, t, line)
    if blocks:
        raise QbcError(f'falta cerrar {blocks[-1]["kind"]}')
    out += b'\x01\x24'
    return bytes(out)


# ---------------------------------------------------------------------------- archivo .qb
def build_qb(items: list, file_name: str) -> bytes:
    """items: (tipo, nombre, valor). Cabecera de 28 bytes como la de WoR."""
    fkey = qbkey(file_name)
    body = bytearray()
    base = 28

    def u32(v):
        return struct.pack('>I', v & 0xFFFFFFFF)

    for kind, name, value in items:
        here = base + len(body)
        if kind == 'script':
            data = value
            body += u32(0x00200700) + u32(qbkey(name)) + u32(fkey) + u32(here + 20) + u32(0)
            body += u32(qbkey(data.hex())) + u32(len(data)) + u32(len(data)) + data
            body += b'\0' * ((-len(body)) % 4)
        elif kind == 'int':
            body += u32(0x00200100) + u32(qbkey(name)) + u32(fkey) + u32(value) + u32(0)
        elif kind == 'float':
            body += u32(0x00200200) + u32(qbkey(name)) + u32(fkey) + struct.pack('>f', value) + u32(0)
        elif kind == 'name':
            body += u32(0x00200D00) + u32(qbkey(name)) + u32(fkey) + u32(qbkey(value)) + u32(0)
        elif kind == 'string':
            s = value.encode('latin1') + b'\0'
            s += b'\0' * ((-len(s)) % 4)
            body += u32(0x00200300) + u32(qbkey(name)) + u32(fkey) + u32(here + 20) + u32(0) + s
        else:
            raise QbcError(f'tipo desconocido {kind}')
    total = base + len(body)
    head = u32(0) + u32(total) + u32(0x1C000000) + u32(0) + u32(len(body)) + u32(0) + u32(0)
    return head + bytes(body)


def compile_source(src: str, file_name: str) -> tuple[bytes, list[str]]:
    toks = lex(src)
    items, names, hooks = [], [], []
    sts = list(statements(toks))
    i = 0
    while i < len(sts):
        st = sts[i]
        w = st[0][1]
        line = st[0][2]
        if w == 'script':
            if len(st) < 2 or st[1][0] != 'name':
                raise QbcError(f'linea {line}: falta el nombre del script')
            name = st[1][1]
            j = i + 1
            while j < len(sts) and not (sts[j][0][1] == 'endscript' and len(sts[j]) == 1):
                j += 1
            if j == len(sts):
                raise QbcError(f'linea {line}: falta endscript en {name}')
            try:
                items.append(('script', name, compile_script(sts[i + 1:j], st[2:])))
            except QbcError as e:
                raise QbcError(f'{name}: {e}') from None
            names.append(name)
            i = j + 1
            continue
        if w in ('int', 'float', 'string', 'name') and len(st) == 4 and st[2][1] == '=':
            n, v = st[1][1], st[3]
            if w == 'int':
                val = int(v[1], 16) if v[0] == 'hex' else int(v[1])
            elif w == 'float':
                val = float(v[1])
            elif w == 'string':
                val = bytes(v[1][1:-1], 'latin1').decode('unicode_escape')
            else:
                val = v[1]
            items.append((w, n, val))
            names.append(n)
            i += 1
            continue
        if w == 'hook' and len(st) > 1 and all(t[0] == 'name' for t in st[1:]):
            hooks.extend(t[1] for t in st[1:])
            i += 1
            continue
        raise QbcError(f'linea {line}: se esperaba script o una global')
    if hooks:   # re-wor renombra estos scripts a re_orig_<nombre> antes de registrar el archivo
        stem = file_name.replace(chr(92), "/").split("/")[-1].split(".")[0]
        items.insert(0, ('string', 're_hooks_' + stem, ' '.join(hooks)))
    return build_qb(items, file_name), names


def main(argv):
    if len(argv) != 2:
        print(__doc__)
        return 2
    src, dst = Path(argv[0]), Path(argv[1])
    try:
        data, names = compile_source(src.read_text(encoding='utf-8'), 'scripts/re_wor/' + src.stem + '.qb')
    except QbcError as e:
        print(f'{src.name}: {e}', file=sys.stderr)
        return 1
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_bytes(data)
    print(f'{src.name}: {len(names)} simbolos, {len(data)} bytes -> {dst}')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
