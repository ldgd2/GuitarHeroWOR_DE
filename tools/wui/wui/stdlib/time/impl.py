"""Implementacion de referencia del tiempo y las tareas."""
from __future__ import annotations

from ...shared.values import Block, FuncRef, TaskRef, is_number, to_text, truthy, type_name


def _secs(ctx, v, what="segundos"):
    if not is_number(v):
        ctx.vm.fault(f"se esperaban {what} (un numero) y llego {type_name(v)} ({to_text(v)})")
    return float(v)


def _fn(ctx, v):
    if not isinstance(v, FuncRef):
        ctx.vm.fault(f"se esperaba una funcion (su nombre, sin parentesis) y llego {type_name(v)}")
    return v


def time(ctx, a, kw): return ctx.vm.time
def dt(ctx, a, kw): return ctx.vm.dt
def frame(ctx, a, kw): return ctx.vm.frame


def wait(ctx, a, kw):
    until = ctx.vm.time + max(0.0, _secs(ctx, a[0]))
    first = ctx.vm.frame
    return Block(lambda vm: (vm.frame > first and vm.time >= until - 1e-9, None), "wait")


def wait_frames(ctx, a, kw):
    target = ctx.vm.frame + max(1, int(_secs(ctx, a[0], "fotogramas")))
    return Block(lambda vm: (vm.frame >= target, None), "wait_frames")


def wait_press(ctx, a, kw):
    timeout, player, buttons = a[0], int(a[1]), [b for b in a[2:]]
    until = ctx.vm.time + _secs(ctx, timeout) if is_number(timeout) and timeout >= 0 else None
    first = ctx.vm.frame

    def poll(vm):
        if vm.frame > first:
            for b in (buttons or vm.pad.buttons(player)):
                if vm.pad.just_pressed(b, player):
                    return True, b
        if until is not None and vm.time >= until:
            return True, None
        return False, None
    return Block(poll, "wait_press")


def wait_release(ctx, a, kw):
    b, player = a[0], int(a[1])
    return Block(lambda vm: (not vm.pad.pressed(b, player), None), "wait_release")


def wait_until(ctx, a, kw):
    fn, timeout = _fn(ctx, a[0]), a[1]
    until = ctx.vm.time + _secs(ctx, timeout) if is_number(timeout) and timeout >= 0 else None

    def poll(vm):
        if truthy(vm.call_now(fn, [])):
            return True, True
        if until is not None and vm.time >= until:
            return True, False
        return False, None
    return Block(poll, "wait_until")


def spawn(ctx, a, kw):
    return ctx.vm.spawn(_fn(ctx, a[0]), list(a[1:]))


def after(ctx, a, kw):
    return ctx.vm.spawn(_fn(ctx, a[1]), list(a[2:]), delay=_secs(ctx, a[0]))


def every_seconds(ctx, a, kw):
    return ctx.vm.spawn(_fn(ctx, a[1]), list(a[2:]), every=max(0.0, _secs(ctx, a[0])))


def _task(ctx, v):
    if not isinstance(v, TaskRef):
        ctx.vm.fault(f"se esperaba una tarea y llego {type_name(v)}")
    return v


def stop(ctx, a, kw):
    if a[0] is not None:
        ctx.vm.stop_task(_task(ctx, a[0]))


def stop_all(ctx, a, kw):
    ctx.vm.stop_all(except_task=ctx.task)


def t_stop(ctx, a, kw):
    ctx.vm.stop_task(_task(ctx, a[0]))


def t_join(ctx, a, kw):
    t, timeout = _task(ctx, a[0]), a[1]
    until = ctx.vm.time + _secs(ctx, timeout) if is_number(timeout) and timeout >= 0 else None

    def poll(vm):
        if not vm.task_running(t):
            return True, vm.task_result(t)
        if until is not None and vm.time >= until:
            return True, None
        return False, None
    return Block(poll, "join")
