"""Deduce el rol (cantante / guitarrista / bajista / baterista) de una animacion de cuerpo .ska.

World Tour no guarda que musico usa cada animacion de un clip; WoR si (struct del clip). En vez de
adivinarlo por el numero de animaciones, se mira QUE huesos mueve la animacion. Todos usan el mismo
esqueleto de 128 huesos, pero:
  * cantante  : mueve los huesos del microfono (122-125) y deja quieta la guitarra (112)
  * guitarra/bajo : mueven bone_guitar_body (112) y la posicion de traste (113-115)
  * bateria   : pista en 113 con poco movimiento, cuerpo sentado
El clasificador se entrena con las ~3500 animaciones de clips de las canciones vanilla de WoR
(ver train_role_model) y se guarda como tabla de pesos en role_model.json (regresion logistica).
"""
from __future__ import annotations

import json
import math
from pathlib import Path

ROLES = ["vocalist", "guitarist", "bassist", "drummer"]
KEY_BONES = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 18, 60, 92, 93, 94,
             112, 113, 114, 115, 122, 123, 124, 125, 126, 127]
MODEL_PATH = Path(__file__).with_name("role_model.json")


def _ptp(vals):
    return (max(vals) - min(vals)) if vals else 0.0


def features(quats: dict, trans: dict, partial) -> list[float]:
    f: list[float] = []
    for b in range(128):
        q = quats.get(b)
        f.append(1.0 if q else 0.0)
        if q and len(q) > 1:
            f.append(min(max(_ptp([k[i] for k in q]) for i in (1, 2, 3)), 30000) / 30000.0)
        else:
            f.append(0.0)
        f.append(1.0 if (partial and (partial[b // 32] >> (b % 32)) & 1) else 0.0)
    for b in KEY_BONES:
        t = trans.get(b) or []
        for i in (1, 2, 3):
            vals = [k[i] for k in t]
            mean = sum(vals) / len(vals) if vals else 0.0
            f.append(max(-200.0, min(200.0, mean)) / 100.0)
            f.append(min(_ptp(vals), 200.0) / 100.0)
        q = quats.get(b) or []
        for i in (1, 2, 3):
            vals = [k[i] for k in q]
            f.append((sum(vals) / len(vals) / 16384.0) if vals else 0.0)
    return f


class RoleModel:
    def __init__(self, data: dict):
        self.classes = data["classes"]
        self.coef = data["coef"]
        self.intercept = data["intercept"]
        self.mean = data["mean"]
        self.scale = data["scale"]

    @classmethod
    def load(cls, path: Path = MODEL_PATH) -> "RoleModel | None":
        return cls(json.loads(path.read_text())) if path.exists() else None

    def proba(self, x: list[float]) -> dict[str, float]:
        z = [(v - m) / s for v, m, s in zip(x, self.mean, self.scale)]
        logits = [sum(w * v for w, v in zip(row, z)) + b for row, b in zip(self.coef, self.intercept)]
        mx = max(logits)
        ex = [math.exp(v - mx) for v in logits]
        tot = sum(ex)
        return {c: e / tot for c, e in zip(self.classes, ex)}


def train_role_model(samples: list[tuple[list[float], str]], path: Path = MODEL_PATH) -> None:
    import numpy as np
    from sklearn.linear_model import LogisticRegression
    from sklearn.preprocessing import StandardScaler
    X = np.array([s[0] for s in samples])
    y = np.array([s[1] for s in samples])
    sc = StandardScaler().fit(X)
    scale = np.where(sc.scale_ == 0, 1.0, sc.scale_)
    clf = LogisticRegression(max_iter=5000, C=0.5, class_weight="balanced").fit((X - sc.mean_) / scale, y)
    path.write_text(json.dumps({"classes": list(clf.classes_), "coef": clf.coef_.tolist(),
                                "intercept": clf.intercept_.tolist(), "mean": sc.mean_.tolist(),
                                "scale": scale.tolist()}))
