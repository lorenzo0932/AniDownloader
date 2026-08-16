import { defineConfig } from 'vitest/config';
import { svelte } from '@sveltejs/vite-plugin-svelte';
import { svelteTesting } from '@testing-library/svelte/vite';

export default defineConfig({
  // base '/' (path ASSOLUTI): Tauri registra gli asset embeddati come
  // /assets/... e la webview li serve dalla root. base './' (commit 6da2a68)
  // rompeva il caricamento nella webview Tauri/WebKitGTK (asset relativi non
  // risolti → pagina bianca su Fedora 43, bug white-screen 2.1.0). La 2.0.1
  // usava path assoluti e funzionava.
  base: '/',
  plugins: [svelte(), svelteTesting()],
  build: {
    outDir: 'dist',
    emptyOutDir: true,
  },
  server: {
    port: 5173,
    proxy: {
      '/api': 'http://localhost:8989',
    },
  },
  test: {
    environment: 'happy-dom',
    include: ['src/**/*.test.js'],
  },
});
