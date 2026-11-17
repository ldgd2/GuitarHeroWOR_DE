// Editor de puesta en escena: React 18 + Vite. `npm run dev` (con el servidor Python en el 8799 para /api)
// o `npm run build` -> dist/, que es lo que sirve `python server.py`.
import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';
import { cpSync } from 'node:fs';

// motor WASM de MediaPipe (camara del PC): va con el editor, sin depender de internet
const mediapipeWasm = () => ({
  name: 'mediapipe-wasm',
  writeBundle() { cpSync('node_modules/@mediapipe/tasks-vision/wasm', 'dist/mediapipe', { recursive: true }); },
});

export default defineConfig({
  plugins: [react(), mediapipeWasm()],
  base: './',
  // paginas de DE Studio: inicio (index.html), editor de canciones (cancion.html) y de interfaces (ui.html)
  build: { outDir: 'dist', emptyOutDir: true, chunkSizeWarningLimit: 2000,
    rollupOptions: { input: { hub: 'index.html', cancion: 'cancion.html', ui: 'ui.html' } } },
  server: { port: 5173, proxy: { '/api': 'http://127.0.0.1:8799' } },
});
