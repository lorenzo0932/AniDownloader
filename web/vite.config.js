import { defineConfig } from 'vitest/config';
import { svelte } from '@sveltejs/vite-plugin-svelte';
import { svelteTesting } from '@testing-library/svelte/vite';

export default defineConfig({
  base: './',
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
