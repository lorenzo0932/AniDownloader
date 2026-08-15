import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';

// Test della risoluzione di BASE isolato in un file dedicato: BASE è una const
// top-level valutata all'import di api.js (linea 2), quindi per coprire entrambi
// i rami servono un dynamic import e il reset del module registry. Il reset è
// fragile sugli upgrade di Vitest (vedi plan/12, sezione rischi): qui è
// circoscritto a questo file e motivato.
const importApi = async () => {
  vi.resetModules();
  return await import('./api.js');
};

describe('BASE resolution', () => {
  const original = globalThis.window;

  beforeEach(() => {
    vi.unstubAllGlobals();
  });

  afterEach(() => {
    vi.unstubAllGlobals();
    globalThis.window = original;
  });

  it('con __TAURI_INTERNALS__: BASE punta a http://127.0.0.1:8989', async () => {
    vi.stubGlobal('window', { __TAURI_INTERNALS__: {} });
    const { BASE } = await importApi();
    expect(BASE).toBe('http://127.0.0.1:8989');
  });

  it('senza __TAURI_INTERNALS__: BASE vuoto (same-origin)', async () => {
    vi.stubGlobal('window', {});
    const { BASE } = await importApi();
    expect(BASE).toBe('');
  });

  it('senza window (headless): BASE vuoto', async () => {
    globalThis.window = undefined;
    const { BASE } = await importApi();
    expect(BASE).toBe('');
  });
});
