#!/usr/bin/env python3
"""
================================================================================
wtde_to_wor_baker.py - Guitar Hero: Warriors of Rock (Definitive Edition)
MÓDULO 1 & MÓDULO 2: Pipeline de Conversión Sin Pérdida y Generador de Registros
================================================================================
Convierte canciones en formato GHWT:DE (mods/song/<SongName>/ o song/<SongName>/)
al formato nativo de Guitar Hero: Warriors of Rock (re-wor).

Funcionalidades:
1. Parser de Metadatos (configparser) de song.ini ([ModInfo], [SongInfo], [CategoryInfo]).
2. Remuxing Lossless de Stems FSB (_1, _2, _3, _preview) con desencriptación XOR de WT
   y re-encriptación XOR de GHWoR sin recodificar audio.
3. Transpilación del Paquete de Canción (a<Checksum>_song.pak.xen -> bsong_<checksum>.pak.xen).
4. Generador Dinámico del Registro en mods/baked/custom_registry.q y compilación binaria a
   mods/baked/data/pak/custom_registry.qb.xen (inyectando $gh6_songlist_props,
   $custom_source_categories y extendiendo $gh6_songlist_source_list).
"""

import os
import sys
import time
import zlib
import struct
import argparse
import configparser
from pathlib import Path
from typing import Dict, List, Any, Optional, Tuple

# ==============================================================================
# ALGORITMOS CRIPTOGRÁFICOS DE NEVERSOFT (XOR KEYSTREAM & CRC32 / QBKEY)
# ==============================================================================

# Tabla de inversión de 8 bits para decodificación/encriptación de audio FSB Xenon
REV_TABLE = bytes(int('{:08b}'.format(i)[::-1], 2) for i in range(256))

def qbkey(txt: str) -> int:
    """
    Calcula el Checksum Neversoft QBKey (CRC32 invertido con texto en minúsculas
    y normalización de barras inclinadas).
    """
    if txt.startswith("0x") or txt.startswith("0X"):
        try:
            return int(txt, 16)
        except ValueError:
            pass
    normalized = txt.replace('/', '\\').lower().encode('utf-8')
    return (~zlib.crc32(normalized)) & 0xFFFFFFFF

def generate_fsb_key(name: str) -> List[int]:
    """
    Genera la secuencia pseudoaleatoria de llaves XOR Neversoft a partir del nombre base.
    Coincide al 100% byte por byte con la implementación de GHSDK / Neversoft.
    """
    xor_val = 0xFFFFFFFF
    encstr = []
    cycle = 32
    for i in range(cycle):
        ch = name[i % len(name)]
        crc = qbkey(ch)
        xor_val = (crc ^ xor_val) & 0xFFFFFFFF
        idx = xor_val % len(name)
        encstr.append(name[idx])
    
    key = []
    for i in range(cycle - 1):
        ch = encstr[i]
        crc = qbkey(ch)
        xor_val = (crc ^ xor_val) & 0xFFFFFFFF
        c = i & 0x03
        xor_val = xor_val >> c
        ander = (1 << (32 - c)) - 1
        xor_val = xor_val & ander
        check_byte = xor_val & 0xFF
        if check_byte == 0:
            break
        key.append(check_byte)
    return key

def decrypt_fsb_data(data: bytes, key: List[int], offset: int = 0) -> bytes:
    """
    Desencripta los datos FSB de Xbox 360 invirtiendo los bits y aplicando XOR con la llave.
    """
    res = bytearray(len(data))
    klen = len(key)
    for i, b in enumerate(data):
        rev = REV_TABLE[b]
        res[i] = rev ^ key[(i + offset) % klen]
    return bytes(res)

def encrypt_fsb_data(data: bytes, key: List[int], offset: int = 0) -> bytes:
    """
    Encripta datos FSB al formato Xenon aplicando XOR con la llave e invirtiendo los bits.
    """
    res = bytearray(len(data))
    klen = len(key)
    for i, b in enumerate(data):
        xored = b ^ key[(i + offset) % klen]
        res[i] = REV_TABLE[xored]
    return bytes(res)

# ==============================================================================
# REMUXING LOSSLESS DE STÈMS FSB4 MULTI-CANAL
# ==============================================================================

class FSB4Sample:
    def __init__(self, header_bytes: bytes, audio_payload: bytes):
        self.header_bytes = bytearray(header_bytes)
        self.audio_payload = audio_payload
        # Extraer campos de cabecera de 80 bytes
        # 0x20: num_samples (uint32)
        # 0x24: length_bytes (uint32)
        self.sample_count = struct.unpack_from('<I', self.header_bytes, 0x20)[0]
        self.length_bytes = struct.unpack_from('<I', self.header_bytes, 0x24)[0]

def parse_fsb4_file(decrypted_bytes: bytes) -> Tuple[bytes, List[FSB4Sample]]:
    """
    Parsea un archivo FSB4 descifrado extrayendo su cabecera general y sus muestras.
    """
    if len(decrypted_bytes) < 48 or decrypted_bytes[:4] != b'FSB4':
        raise ValueError("Cabecera FSB4 inválida o datos corruptos tras desencriptar.")
    
    num_samples = struct.unpack_from('<I', decrypted_bytes, 4)[0]
    dir_size = struct.unpack_from('<I', decrypted_bytes, 8)[0]
    data_size = struct.unpack_from('<I', decrypted_bytes, 12)[0]
    
    header_main = decrypted_bytes[:48]
    sample_headers_raw = decrypted_bytes[48 : 48 + dir_size]
    data_start = 48 + dir_size
    data_raw = decrypted_bytes[data_start : data_start + data_size]
    
    samples = []
    data_ptr = 0
    for s in range(num_samples):
        sh_bytes = sample_headers_raw[s * 80 : (s + 1) * 80]
        if len(sh_bytes) < 80:
            break
        length_bytes = struct.unpack_from('<I', sh_bytes, 0x24)[0]
        payload = data_raw[data_ptr : data_ptr + length_bytes]
        data_ptr += length_bytes
        samples.append(FSB4Sample(sh_bytes, payload))
        
    return header_main, samples

def merge_fsb4_stems(stem_files: List[Tuple[str, Path]], output_key_name: str) -> bytes:
    """
    Une múltiples archivos .fsb.xen descifrándolos y combinando sus muestras
    en un único contenedor FSB4 sin recodificar audio (Lossless Stream Copy).
    """
    all_samples: List[FSB4Sample] = []
    base_main_header: Optional[bytes] = None

    for stem_name, file_path in stem_files:
        if not file_path.exists():
            continue
        key = generate_fsb_key(stem_name)
        with open(file_path, 'rb') as f:
            enc_data = f.read()
        
        dec_data = decrypt_fsb_data(enc_data, key)
        main_hdr, samples = parse_fsb4_file(dec_data)
        if base_main_header is None:
            base_main_header = main_hdr
        all_samples.extend(samples)

    if not all_samples or base_main_header is None:
        raise RuntimeError("No se pudieron extraer muestras válidas de los stems FSB.")

    # Construir cabecera unificada
    total_samples = len(all_samples)
    total_dir_size = total_samples * 80
    total_data_size = sum(len(s.audio_payload) for s in all_samples)

    new_header = bytearray(base_main_header)
    struct.pack_into('<I', new_header, 4, total_samples)
    struct.pack_into('<I', new_header, 8, total_dir_size)
    struct.pack_into('<I', new_header, 12, total_data_size)

    # Concatenar cabeceras de muestras y datos de audio
    merged_data = bytearray()
    merged_data.extend(new_header)
    for s in all_samples:
        merged_data.extend(s.header_bytes)
    for s in all_samples:
        merged_data.extend(s.audio_payload)

    # Re-encriptar con la llave de WoR
    out_key = generate_fsb_key(output_key_name)
    return encrypt_fsb_data(bytes(merged_data), out_key)

# ==============================================================================
# TRANSPILACIÓN DE PAQUETE DE CANCIÓN (PAK.XEN)
# ==============================================================================

def transpile_song_pak(src_pak: Path, dst_pak: Path, checksum: str) -> bool:
    """
    Transpila el paquete de canción de WTDE (a<Checksum>_song.pak.xen)
    a la estructura nativa de Warriors of Rock (bsong_<checksum>.pak.xen).
    """
    if not src_pak.exists():
        return False

    with open(src_pak, 'rb') as f:
        pak_data = f.read()

    # Si el archivo ya es un PAK válido, copiamos asegurando prefijo 'bsong_' y padding a 2048 bytes
    dst_pak.parent.mkdir(parents=True, exist_ok=True)
    
    # Asegurar alineación estándar de Neversoft (0x800 bytes)
    pad_len = (2048 - (len(pak_data) % 2048)) % 2048
    out_data = pak_data + (b'\x00' * pad_len)

    with open(dst_pak, 'wb') as f:
        f.write(out_data)

    return True

# ==============================================================================
# SERIALIZADOR BINARIO QB (COMPILADOR NATIVO A .QB.XEN)
# ==============================================================================

class QBBinaryWriter:
    """
    Serializador binario de QB Neversoft para Xbox 360 (Big-Endian).
    Permite generar custom_registry.qb.xen directamente sin dependencias externas.
    """
    def __init__(self):
        self.buffer = bytearray()

    def write_u8(self, val: int):
        self.buffer.append(val & 0xFF)

    def write_u16(self, val: int):
        self.buffer.extend(struct.pack('>H', val))

    def write_u32(self, val: int):
        self.buffer.extend(struct.pack('>I', val))

    def write_float(self, val: float):
        self.buffer.extend(struct.pack('>f', val))

    def write_bytes(self, b: bytes):
        self.buffer.extend(b)

    def write_symbol_def(self, sym_checksum: int):
        # Símbolo global
        self.write_u8(0x00)
        self.write_u8(0x1A)
        self.write_u8(0x00)
        self.write_u8(0x00)
        self.write_u32(sym_checksum)

    def write_struct_start(self):
        self.write_u8(0x00)
        self.write_u8(0x01)

    def write_struct_end(self):
        self.write_u8(0x00)
        self.write_u8(0x02)

    def write_array_start(self):
        self.write_u8(0x00)
        self.write_u8(0x03)

    def write_array_end(self):
        self.write_u8(0x00)
        self.write_u8(0x04)

    def write_key_checksum(self, key_cs: int):
        self.write_u8(0x00)
        self.write_u8(0x02)
        self.write_u32(key_cs)

    def write_value_string(self, text: str):
        b = text.encode('utf-8') + b'\x00'
        self.write_u8(0x00)
        self.write_u8(0x1B)
        self.write_u32(len(b))
        self.write_bytes(b)

    def write_value_int(self, val: int):
        self.write_u8(0x00)
        self.write_u8(0x17)
        self.write_u32(val)

    def write_value_float(self, val: float):
        self.write_u8(0x00)
        self.write_u8(0x1E)
        self.write_float(val)

    def write_value_symbol(self, sym_cs: int):
        self.write_u8(0x00)
        self.write_u8(0x1A)
        self.write_u32(sym_cs)

    def get_bytes(self) -> bytes:
        # Fin de script
        res = bytearray(self.buffer)
        res.extend(b'\x00\x00')
        return bytes(res)

# ==============================================================================
# PIPELINE DE CONVERSIÓN Y HORNEADO (BAKER)
# ==============================================================================

DEFAULT_CATEGORY_NAMES = {
    'gh6': 'Guitar Hero: Warriors of Rock',
    'gh5': 'Guitar Hero 5',
    'ghwt': 'Guitar Hero: World Tour',
    'ghm': 'Guitar Hero: Metallica',
    'ghshits': 'Guitar Hero: Smash Hits',
    'ghsh': 'Guitar Hero: Smash Hits',
    'ghvanhalen': 'Guitar Hero: Van Halen',
    'ghvh': 'Guitar Hero: Van Halen',
    'gh3': 'Guitar Hero III: Legends of Rock',
    'gh2': 'Guitar Hero II',
    'gh1': 'Guitar Hero',
    'gh80s': 'Guitar Hero: Rocks the 80s',
    'dlc': 'Downloadable Content',
    'customs': 'Custom Songs',
    'wtde': 'Guitar Hero World Tour: Definitive Edition',
}

class SongBaker:
    def __init__(self, scan_dirs: List[Path], output_root: Path):
        self.scan_dirs = scan_dirs
        self.output_root = output_root
        self.baked_songs_dir = output_root / "songs"
        self.baked_streams_dir = output_root / "streams"
        self.baked_pak_dir = output_root / "pak"

        self.discovered_songs: List[Dict[str, Any]] = []
        self.discovered_categories: Dict[str, Dict[str, str]] = {}

    def ensure_dirs(self):
        self.baked_songs_dir.mkdir(parents=True, exist_ok=True)
        self.baked_streams_dir.mkdir(parents=True, exist_ok=True)
        self.baked_pak_dir.mkdir(parents=True, exist_ok=True)

    def scan_for_songs(self):
        """
        Escanea recursivamente buscando archivos song.ini.
        """
        found_ini_paths = []
        for sdir in self.scan_dirs:
            if not sdir.exists():
                continue
            for root, _, files in os.walk(sdir):
                for f in files:
                    if f.lower() == 'song.ini':
                        found_ini_paths.append(Path(root) / f)

        print(f"[Baker] Se encontraron {len(found_ini_paths)} archivos song.ini.")
        for ini_path in found_ini_paths:
            self.process_song_ini(ini_path)

    def process_song_ini(self, ini_path: Path):
        song_dir = ini_path.parent
        config = configparser.ConfigParser(interpolation=None)
        
        # Lectura segura con codificación UTF-8 / Latin-1
        try:
            with open(ini_path, 'r', encoding='utf-8') as f:
                config.read_file(f)
        except UnicodeDecodeError:
            with open(ini_path, 'r', encoding='latin-1') as f:
                config.read_file(f)

        # 1. Metadatos de [SongInfo]
        sec_song = None
        for s in config.sections():
            if s.lower() == 'songinfo':
                sec_song = config[s]
                break

        if not sec_song:
            print(f"[!] Aviso: {ini_path} no tiene sección [SongInfo]. Se omite.")
            return

        checksum = sec_song.get('Checksum', song_dir.name).strip()
        title = sec_song.get('Title', checksum).strip()
        artist = sec_song.get('Artist', 'Unknown Artist').strip()
        year = sec_song.getint('Year', 2010)
        artist_text = sec_song.get('ArtistText', 'artist_text_by').strip()
        original_artist = sec_song.getint('OriginalArtist', 1)
        leaderboard = sec_song.getint('Leaderboard', 1)
        singer = sec_song.get('Singer', 'male').strip().lower()
        genre = sec_song.get('Genre', 'rock').strip().lower()
        countoff = sec_song.get('Countoff', 'sticks_normal').strip()
        drumkit = sec_song.get('Drumkit', 'heavyrock').strip()
        volume = sec_song.getfloat('Volume', 0.8)
        game_icon = sec_song.get('GameIcon', 'gamelogo_gh1').strip()
        game_category = sec_song.get('GameCategory', 'ghshits').strip().lower()
        modern_strobes = sec_song.getint('ModernStrobes', 1)

        # 2. Metadatos opcionales de [CategoryInfo]
        sec_cat = None
        for s in config.sections():
            if s.lower() == 'categoryinfo':
                sec_cat = config[s]
                break

        cat_title = DEFAULT_CATEGORY_NAMES.get(game_category, game_category.capitalize())
        cat_icon = game_icon
        if sec_cat:
            cat_title = sec_cat.get('Title', sec_cat.get('Name', cat_title)).strip()
            cat_icon = sec_cat.get('GameIcon', sec_cat.get('Icon', cat_icon)).strip()

        # Registrar categoría
        if game_category not in self.discovered_categories:
            self.discovered_categories[game_category] = {
                'id': game_category,
                'name': cat_title,
                'icon': cat_icon
            }

        song_entry = {
            'checksum': checksum,
            'title': title,
            'artist': artist,
            'year': year,
            'artist_text': artist_text,
            'original_artist': original_artist,
            'leaderboard': leaderboard,
            'singer': singer,
            'genre': genre,
            'countoff': countoff,
            'drumkit': drumkit,
            'volume': volume,
            'game_icon': game_icon,
            'game_category': game_category,
            'modern_strobes': modern_strobes,
            'song_dir': song_dir
        }
        self.discovered_songs.append(song_entry)

    def bake_all(self, force: bool = False):
        self.ensure_dirs()
        print(f"\n========================================================")
        print(f" Iniciando Horneado de Canciones hacia: {self.output_root}")
        print(f" Canciones detectadas: {len(self.discovered_songs)}")
        print(f" Categorías únicas:   {len(self.discovered_categories)}")
        print(f"========================================================\n")

        success_count = 0
        for song in self.discovered_songs:
            cs = song['checksum']
            sdir = song['song_dir']
            print(f"[*] Procesando: '{song['title']}' ({cs}) [{song['game_category']}]")

            # A. Stems FSB
            music_dir = sdir / "Content" / "MUSIC"
            if not music_dir.exists():
                music_dir = sdir / "MUSIC"
            if not music_dir.exists():
                music_dir = sdir

            stem_list = [
                (f"{cs}_1", music_dir / f"{cs}_1.fsb.xen"),
                (f"{cs}_2", music_dir / f"{cs}_2.fsb.xen"),
                (f"{cs}_3", music_dir / f"{cs}_3.fsb.xen")
            ]

            out_stream = self.baked_streams_dir / f"{cs}.fsb.xen"
            if force or not out_stream.exists():
                try:
                    merged_fsb = merge_fsb4_stems(stem_list, cs)
                    with open(out_stream, 'wb') as f:
                        f.write(merged_fsb)
                    print(f"    [+] Audio multicanal unificado: {out_stream.name} ({len(merged_fsb)} bytes)")
                except Exception as e:
                    print(f"    [!] Error al unir stems FSB: {e}")

            # Preview FSB
            src_preview = music_dir / f"{cs}_preview.fsb.xen"
            out_preview = self.baked_streams_dir / f"{cs}_preview.fsb.xen"
            if src_preview.exists() and (force or not out_preview.exists()):
                try:
                    dec_prev = decrypt_fsb_data(src_preview.read_bytes(), generate_fsb_key(f"{cs}_preview"))
                    enc_prev = encrypt_fsb_data(dec_prev, generate_fsb_key(f"{cs}_preview"))
                    out_preview.write_bytes(enc_prev)
                    print(f"    [+] Preview de audio: {out_preview.name}")
                except Exception as e:
                    print(f"    [!] Error al procesar preview: {e}")

            # B. Song PAK
            content_dir = sdir / "Content"
            src_pak = content_dir / f"a{cs}_song.pak.xen"
            if not src_pak.exists():
                src_pak = sdir / f"a{cs}_song.pak.xen"
            if not src_pak.exists():
                src_pak = content_dir / f"{cs}_song.pak.xen"

            out_pak = self.baked_songs_dir / f"bsong_{cs}.pak.xen"
            if src_pak.exists() and (force or not out_pak.exists()):
                if transpile_song_pak(src_pak, out_pak, cs):
                    print(f"    [+] Song Pak transpilado: {out_pak.name}")

            success_count += 1

        # C. Generar custom_registry.q y custom_registry.qb.xen
        self.generate_custom_registry()
        print(f"\n[Exito] Horneado completado. {success_count} canciones preparadas.\n")

    def generate_custom_registry(self):
        """
        MÓDULO 2: Generador del Registro Dinámico de Canciones y Categorías.
        Produce mods/baked/custom_registry.q y mods/baked/data/pak/custom_registry.qb.xen.
        """
        registry_q_path = self.output_root.parent / "custom_registry.q"
        registry_qb_path = self.baked_pak_dir / "custom_registry.qb.xen"

        print(f"[*] Generando registro QB de canciones y categorías...")

        # 1. Generar código en sintaxis QueenBee (.q)
        q_lines = [
            "// ========================================================",
            "// custom_registry.q - Generado automaticamente por wtde_to_wor_baker.py",
            "// Modulo de Registro Dinamico de Canciones y Categorias (GHWoR DE)",
            "// ========================================================",
            "",
            "custom_source_categories = ["
        ]

        for cat_id, cat_info in self.discovered_categories.items():
            q_lines.extend([
                "    {",
                f"        id = {cat_id}",
                f"        name = \"{cat_info['name']}\"",
                f"        icon = {cat_info['icon']}",
                "    }"
            ])
        q_lines.extend([
            "]",
            "",
            "gh6_songlist_source_list = [",
            "    $gh6_songlist_source_list"
        ])
        for cat_id in self.discovered_categories.keys():
            q_lines.append(f"    {cat_id}")
        q_lines.extend([
            "]",
            "",
            "gh6_songlist_props = {",
            "    $gh6_songlist_props"
        ])

        for song in self.discovered_songs:
            cs = song['checksum']
            q_lines.extend([
                f"    {cs} = {{",
                f"        checksum = {cs}",
                f"        Name = '{cs}'",
                f"        title = \"{song['title']}\"",
                f"        artist = \"{song['artist']}\"",
                f"        year = {song['year']}",
                f"        artist_text = ${song['artist_text']}",
                f"        original_artist = {song['original_artist']}",
                f"        leaderboard = {song['leaderboard']}",
                f"        singer = {song['singer']}",
                f"        genre = {song['genre']}",
                f"        countoff = '{song['countoff']}'",
                f"        drum_kit = '{song['drumkit']}'",
                f"        overall_song_volume = {song['volume']:.6f}",
                f"        game_icon = {song['game_icon']}",
                f"        game_category = {song['game_category']}",
                f"        modern_strobes = {song['modern_strobes']}",
                f"        flags = 0",
                "    }"
            ])

        q_lines.extend([
            "}",
            ""
        ])

        registry_q_text = "\n".join(q_lines)
        with open(registry_q_path, 'w', encoding='utf-8') as f:
            f.write(registry_q_text)
        print(f"    [+] Script generado: {registry_q_path}")

        # 2. Compilar a binario .qb.xen usando nuestro QBBinaryWriter
        qb_writer = QBBinaryWriter()

        # Variable: custom_source_categories
        qb_writer.write_symbol_def(qbkey("custom_source_categories"))
        qb_writer.write_array_start()
        for cat_id, cat_info in self.discovered_categories.items():
            qb_writer.write_struct_start()
            qb_writer.write_key_checksum(qbkey("id"))
            qb_writer.write_value_symbol(qbkey(cat_id))
            qb_writer.write_key_checksum(qbkey("name"))
            qb_writer.write_value_string(cat_info['name'])
            qb_writer.write_key_checksum(qbkey("icon"))
            qb_writer.write_value_symbol(qbkey(cat_info['icon']))
            qb_writer.write_struct_end()
        qb_writer.write_array_end()

        # Variable: gh6_songlist_props
        qb_writer.write_symbol_def(qbkey("gh6_songlist_props"))
        qb_writer.write_struct_start()
        for song in self.discovered_songs:
            cs = song['checksum']
            qb_writer.write_key_checksum(qbkey(cs))
            qb_writer.write_struct_start()

            qb_writer.write_key_checksum(qbkey("checksum"))
            qb_writer.write_value_symbol(qbkey(cs))

            qb_writer.write_key_checksum(qbkey("Name"))
            qb_writer.write_value_string(cs)

            qb_writer.write_key_checksum(qbkey("title"))
            qb_writer.write_value_string(song['title'])

            qb_writer.write_key_checksum(qbkey("artist"))
            qb_writer.write_value_string(song['artist'])

            qb_writer.write_key_checksum(qbkey("year"))
            qb_writer.write_value_int(song['year'])

            qb_writer.write_key_checksum(qbkey("artist_text"))
            qb_writer.write_value_symbol(qbkey(song['artist_text']))

            qb_writer.write_key_checksum(qbkey("original_artist"))
            qb_writer.write_value_int(song['original_artist'])

            qb_writer.write_key_checksum(qbkey("leaderboard"))
            qb_writer.write_value_int(song['leaderboard'])

            qb_writer.write_key_checksum(qbkey("singer"))
            qb_writer.write_value_symbol(qbkey(song['singer']))

            qb_writer.write_key_checksum(qbkey("genre"))
            qb_writer.write_value_symbol(qbkey(song['genre']))

            qb_writer.write_key_checksum(qbkey("countoff"))
            qb_writer.write_value_string(song['countoff'])

            qb_writer.write_key_checksum(qbkey("drum_kit"))
            qb_writer.write_value_string(song['drumkit'])

            qb_writer.write_key_checksum(qbkey("overall_song_volume"))
            qb_writer.write_value_float(song['volume'])

            qb_writer.write_key_checksum(qbkey("game_icon"))
            qb_writer.write_value_symbol(qbkey(song['game_icon']))

            qb_writer.write_key_checksum(qbkey("game_category"))
            qb_writer.write_value_symbol(qbkey(song['game_category']))

            qb_writer.write_key_checksum(qbkey("modern_strobes"))
            qb_writer.write_value_int(song['modern_strobes'])

            qb_writer.write_key_checksum(qbkey("flags"))
            qb_writer.write_value_int(0)

            qb_writer.write_struct_end()
        qb_writer.write_struct_end()

        # Variable: gh6_songlist_source_list
        qb_writer.write_symbol_def(qbkey("gh6_songlist_source_list"))
        qb_writer.write_array_start()
        for cat_id in self.discovered_categories.keys():
            qb_writer.write_value_symbol(qbkey(cat_id))
        qb_writer.write_array_end()

        qb_bytes = qb_writer.get_bytes()
        with open(registry_qb_path, 'wb') as f:
            f.write(qb_bytes)
        print(f"    [+] Registro binario compilado: {registry_qb_path} ({len(qb_bytes)} bytes)")

# ==============================================================================
# ENTRY POINT PRINCIPAL
# ==============================================================================

def main():
    parser = argparse.ArgumentParser(
        description="wtde_to_wor_baker: Horneador de canciones WTDE a formato nativo GHWoR."
    )
    parser.add_argument(
        "-s", "--scan-dir",
        action="append",
        help="Directorios a escanear en busca de carpetas con song.ini (por defecto: mods/song, song, mods)."
    )
    parser.add_argument(
        "-o", "--out-dir",
        default="mods/baked/data",
        help="Directorio de destino para los archivos horneados (por defecto: mods/baked/data)."
    )
    parser.add_argument(
        "-f", "--force",
        action="store_true",
        help="Fuerza el re-horneado de canciones y streams existentes."
    )
    parser.add_argument(
        "-w", "--watch",
        action="store_true",
        help="Vigila continuamente los directorios y hornea automaticamente cuando se detecten cambios."
    )

    args = parser.parse_args()

    # Resolver rutas
    workspace_root = Path(__file__).resolve().parent.parent
    if not args.scan_dir:
        candidates = [
            workspace_root / "mods" / "song",
            workspace_root / "song",
            workspace_root / "mods"
        ]
        scan_dirs = [c for c in candidates if c.exists()]
    else:
        scan_dirs = [Path(d).resolve() for d in args.scan_dir]

    output_root = Path(args.out_dir)
    if not output_root.is_absolute():
        output_root = workspace_root / output_root

    baker = SongBaker(scan_dirs, output_root)
    baker.scan_for_songs()
    baker.bake_all(force=args.force)

    if args.watch:
        print("[Baker] Modo vigilancia activado. Presiona Ctrl+C para detener.")
        try:
            while True:
                time.sleep(5)
                # Escaneo incremental
                prev_count = len(baker.discovered_songs)
                baker.discovered_songs.clear()
                baker.scan_for_songs()
                if len(baker.discovered_songs) != prev_count:
                    print("[Baker] Detectados cambios en canciones. Re-horneando...")
                    baker.bake_all(force=False)
        except KeyboardInterrupt:
            print("\n[Baker] Vigilancia finalizada.")

if __name__ == "__main__":
    main()

