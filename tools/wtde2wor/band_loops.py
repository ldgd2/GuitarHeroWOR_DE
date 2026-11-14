"""Linea de tiempo de loops de la banda (Band_PlayLoop) para una cancion convertida.

En WoR cada musico necesita eventos Band_PlayLoop: al empezar la cancion pasan del loop de espera
(t=0) al de tocar, y en cada seccion cambian entre loops mas tranquilos o mas intensos. World Tour los
elige solo, asi que las canciones de WT no traen esos eventos y sin ellos la banda se queda congelada
en la pose de espera.

Metodo: en la cancion vanilla que hace de plantilla se mide la intensidad (notas de guitarra experto
por segundo) mientras suena cada loop de cada musico. Luego, en cada seccion de la cancion de WT, a
cada musico se le da el loop de la plantilla cuya intensidad relativa (respecto al maximo de su
cancion) mas se parece a la de la seccion.
"""
from __future__ import annotations

import bisect
import copy
import struct

from qb_io import QbArray, QbStruct
from wt_chart import qbkey

K_TIME, K_SCR, K_PARAMS = qbkey("time"), qbkey("scr"), qbkey("params")
K_NAME = qbkey("name")
PLAY_LOOP = qbkey("Band_PlayLoop")
FORCE_IDLE = 0xAA18B589  # Band_ForceAllToIdle
MOOD = 0x5B3942DC        # cambio de expresion facial ('_happy', '_rocking'...)


def wor_note_times(note: bytes, section: str = "guitarexpertinstrument") -> list[int]:
    n = struct.unpack_from(">I", note, 8)[0]
    off = 28
    want = qbkey(section)
    for _ in range(n):
        name, cnt, _typ, size = struct.unpack_from(">IIII", note, off)
        off += 16
        if name == want:
            return sorted(struct.unpack_from(">I", note, off + i * size)[0] for i in range(cnt))
        off += cnt * size
    return []


def _density(times: list[int], t0: int, t1: int) -> float:
    if t1 <= t0:
        return 0.0
    return (bisect.bisect_left(times, t1) - bisect.bisect_left(times, t0)) * 1000.0 / (t1 - t0)


def _loop_key(ev: QbStruct) -> tuple:
    p = ev.get(K_PARAMS)
    return tuple((m.name, m.value) for m in p.members) if isinstance(p, QbStruct) else ()


def template_loops(events: list, note_times: list[int]):
    """{musico: [(intensidad_relativa, evento_plantilla)]} con los loops de cancion (no los de t=0)."""
    end = (note_times[-1] + 2000) if note_times else 0
    loops = [e for e in events if e.get(K_SCR) == PLAY_LOOP]
    by_member: dict[int, list] = {}
    for e in loops:
        by_member.setdefault(e.get(K_PARAMS).get(K_NAME), []).append(e)
    peak = max((_density(note_times, t, t + 10000) for t in range(0, end, 2000)), default=1.0) or 1.0
    out: dict[int, list] = {}
    for member, evs in by_member.items():
        evs.sort(key=lambda e: e.get(K_TIME, 0))
        acc: dict[tuple, list] = {}
        for i, e in enumerate(evs):
            t0 = e.get(K_TIME, 0)
            if t0 == 0:
                continue  # loop de espera previo a la cancion
            t1 = evs[i + 1].get(K_TIME, 0) if i + 1 < len(evs) else end
            if t1 <= t0:
                continue
            a = acc.setdefault(_loop_key(e), [0.0, 0, e])
            a[0] += _density(note_times, t0, t1) * (t1 - t0)
            a[1] += t1 - t0
        out[member] = [(v[0] / v[1] / peak, v[2]) for v in acc.values() if v[1] > 0]
    return out


def song_start_events(events: list) -> list:
    """Eventos de arranque de la plantilla en su primer instante > 0 (caras de animo, etc.),
    sin clips ni loops (esos se generan aparte)."""
    times = sorted({e.get(K_TIME, 0) for e in events if e.get(K_TIME, 0) > 0})
    if not times:
        return []
    t = times[0]
    return [e for e in events if e.get(K_TIME, 0) == t and e.get(K_SCR) == MOOD]


def build_loop_events(template_events: list, template_note_times: list[int], wt_note_times: list[int],
                      sections: list[int], force_idle_times: list[int]) -> list:
    """Eventos Band_PlayLoop (y de animo) para la cancion de WT."""
    if not wt_note_times:
        return []
    loops = template_loops(template_events, template_note_times)
    if not loops:
        return []
    wt_end = wt_note_times[-1] + 2000
    first = max(0, wt_note_times[0] - 1500)
    peak = max((_density(wt_note_times, t, t + 10000) for t in range(0, wt_end, 2000)), default=1.0) or 1.0
    bounds = sorted({first, *[s for s in sections if first < s < wt_end]})
    out = []
    for ev in song_start_events(template_events):
        e = copy.deepcopy(ev)
        _set_time(e, first)
        out.append(e)
    current: dict[int, QbStruct] = {}
    for i, t0 in enumerate(bounds):
        t1 = bounds[i + 1] if i + 1 < len(bounds) else wt_end
        rel = _density(wt_note_times, t0, t1) / peak
        for member, options in loops.items():
            if not options:
                continue
            _, best = min(options, key=lambda o: abs(o[0] - rel))
            if member in current and _loop_key(current[member]) == _loop_key(best):
                continue
            e = copy.deepcopy(best)
            _set_time(e, t0)
            out.append(e)
            current[member] = best
    # Tras un Band_ForceAllToIdle (fin de clip en WT) se restablecen los loops vigentes.
    for t in force_idle_times:
        for member in loops:
            active = None
            for e in out:
                if e.get(K_SCR) == PLAY_LOOP and e.get(K_PARAMS).get(K_NAME) == member and e.get(K_TIME, 0) <= t:
                    active = e
            if active is not None:
                e = copy.deepcopy(active)
                _set_time(e, t + 1)
                out.append(e)
    return out


def _set_time(ev: QbStruct, t: int) -> None:
    for m in ev.members:
        if m.name == K_TIME:
            m.value = t
            return


def wt_sections(wt_qb, wt_name: str, fretbars: list[int]) -> list[int]:
    """Inicios de seccion de la cancion de WT (marcadores de guitarra; si no hay, cada 8 compases)."""
    it = wt_qb.find(qbkey(f"{wt_name}_guitar_markers"))
    if it is not None and isinstance(it.value, QbArray) and it.value.elem == "struct":
        ts = sorted(s.get(K_TIME, 0) for s in it.value.values)
        if ts:
            return ts
    return fretbars[::32]
