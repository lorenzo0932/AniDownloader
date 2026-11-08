import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen, fireEvent, waitFor } from '@testing-library/svelte';
import StoragePanel from './StoragePanel.svelte';

// happy-dom non ha la Web Animations API usata dalle transizioni del modale.
if (typeof Element !== 'undefined' && !Element.prototype.animate) {
  Element.prototype.animate = function () {
    const anim = {
      currentTime: 0,
      playState: 'finished',
      onfinish: null,
      cancel() {}, play() {}, pause() {}, reverse() {},
      finished: null,
    };
    anim.finished = Promise.resolve().then(() => {
      anim.onfinish?.();
    });
    return anim;
  };
}

const { infoMock, cleanupMock } = vi.hoisted(() => ({
  infoMock: vi.fn(),
  cleanupMock: vi.fn(),
}));

vi.mock('../api.js', () => ({
  api: {
    storage: { info: infoMock, cleanup: cleanupMock },
  },
}));

const dati = {
  freeBytes: 500 * 1024 * 1024 * 1024,
  totalBytes: 1000 * 1024 * 1024 * 1024,
  cache: { files: 12, bytes: 2 * 1024 * 1024 },
  partial: { files: 3, bytes: 1024 },
  series: [
    { name: 'One Piece', path: '/v/One Piece', files: 40, bytes: 8 * 1024 * 1024 * 1024,
      partialFiles: 0, partialBytes: 0 },
    { name: 'Naruto', path: '/v/Naruto', files: 10, bytes: 2 * 1024 * 1024 * 1024,
      partialFiles: 1, partialBytes: 1024 },
  ],
};

beforeEach(() => {
  vi.clearAllMocks();
  infoMock.mockResolvedValue(dati);
  cleanupMock.mockResolvedValue({ success: true, removedFiles: 12, freedBytes: 2 * 1024 * 1024 });
});

describe('StoragePanel', () => {
  it('mostra disco, cache, parziali e serie', async () => {
    render(StoragePanel);
    expect(await screen.findByText('Cache miniature')).toBeTruthy();
    expect(screen.getByText('2.0 MB')).toBeTruthy();      // cache
    expect(screen.getByText('3 residui (.aria2/.part)')).toBeTruthy();
    expect(screen.getByText('One Piece')).toBeTruthy();
    expect(screen.getByText('Naruto')).toBeTruthy();
    expect(screen.getByText('Le azioni sono immediate; non serve Salva.')).toBeTruthy();
  });

  it('barra disco: percentuale usata e aria-valuenow coerenti', async () => {
    render(StoragePanel);
    await screen.findByText('Cache miniature');

    // 500 GB liberi su 1000 GB totali → 50% usato.
    const barra = screen.getByRole('progressbar', { name: 'Utilizzo disco' });
    expect(barra.getAttribute('aria-valuenow')).toBe('50');
    expect(barra.getAttribute('aria-valuemin')).toBe('0');
    expect(barra.getAttribute('aria-valuemax')).toBe('100');
    expect(screen.getByText('liberi su 1000.0 GB (50% usato)')).toBeTruthy();
  });

  it('svuota la cache solo dopo conferma', async () => {
    render(StoragePanel);
    await screen.findByText('Cache miniature');
    await fireEvent.click(screen.getByRole('button', { name: 'Svuota cache' }));
    expect(screen.getByText('Svuotare la cache miniature?')).toBeTruthy();
    await fireEvent.click(screen.getByRole('button', { name: 'Conferma' }));
    await waitFor(() => expect(cleanupMock).toHaveBeenCalledWith('cache'));
    await waitFor(() => expect(infoMock).toHaveBeenCalledTimes(2));
  });

  it('rimuove i parziali con l\'azione dedicata', async () => {
    render(StoragePanel);
    await screen.findByText('Cache miniature');
    await fireEvent.click(screen.getByRole('button', { name: 'Rimuovi parziali' }));
    await fireEvent.click(screen.getByRole('button', { name: 'Conferma' }));
    await waitFor(() => expect(cleanupMock).toHaveBeenCalledWith('parziali'));
  });

  it('flush: senza dati disco la barra resta a 0%', async () => {
    infoMock.mockResolvedValue({ freeBytes: 0, totalBytes: 0, cache: {}, partial: {}, series: [] });
    render(StoragePanel);
    await screen.findByText('Cache miniature');
    const barra = screen.getByRole('progressbar', { name: 'Utilizzo disco' });
    expect(barra.getAttribute('aria-valuenow')).toBe('0');
  });
});
