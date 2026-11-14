// Editor de puesta en escena: React 18 + Vite. `npm run dev` (con el servidor Python en el 8799 para /api)
// o `npm run build` -> dist/, que es lo que sirve `python server.py`.
import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

export default defineConfig({
  plugins: [react()],
  base: './',
  build: { outDir: 'dist', emptyOutDir: true, chunkSizeWarningLimit: 2000 },
  server: { port: 5173, proxy: { '/api': 'http://127.0.0.1:8799' } },
});
