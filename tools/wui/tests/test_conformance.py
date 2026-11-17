"""Las tres maquinas (Python, JS del editor y C++ de re-wor) dan el mismo resultado en tests/conformance/cases.py.

JS se prueba si hay `node`; C++ si hay `clang++` (o $CXX). Si no estan, la prueba se salta.
En Windows, clang++ necesita el entorno de Visual Studio (vcvars64) para encontrar la libreria estandar.
"""
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent / "conformance"
NODE = shutil.which("node") or (r"C:\Program Files\nodejs\node.exe" if Path(r"C:\Program Files\nodejs\node.exe").is_file() else None)
CXX = os.environ.get("CXX") or shutil.which("clang++")


class Conformance(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.dir = Path(tempfile.mkdtemp(prefix="wuiconf"))
        subprocess.run([sys.executable, str(HERE / "trace.py"), str(cls.dir)], check=True)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.dir, ignore_errors=True)

    @unittest.skipUnless(NODE, "sin node")
    def test_js(self):
        p = subprocess.run([NODE, str(HERE / "run_js.mjs"), str(self.dir)], capture_output=True, text=True, encoding="utf-8")
        self.assertEqual(p.returncode, 0, p.stdout + p.stderr)

    @unittest.skipUnless(CXX, "sin clang++")
    def test_cpp(self):
        p = subprocess.run([sys.executable, str(HERE / "run_cpp.py"), str(self.dir)], capture_output=True, text=True,
                           encoding="utf-8", env={**os.environ, "CXX": CXX})
        self.assertEqual(p.returncode, 0, p.stdout + p.stderr)


if __name__ == "__main__":
    unittest.main()
