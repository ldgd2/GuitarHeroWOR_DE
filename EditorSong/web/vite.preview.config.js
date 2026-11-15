// Paquete de la vista previa 3D de la app del movil (preview/main.js -> vcam_app/assets/preview/preview.js).
//   npx vite build -c vite.preview.config.js
import { defineConfig } from 'vite';
import { fileURLToPath } from 'node:url';

export default defineConfig({
  publicDir: false,
  build: {
    outDir: fileURLToPath(new URL('../vcam_app/assets/preview', import.meta.url)),
    emptyOutDir: false,
    lib: { entry: fileURLToPath(new URL('preview/main.js', import.meta.url)), formats: ['iife'], name: 'GHPreview', fileName: () => 'preview.js' },
    target: 'es2019',
  },
});
