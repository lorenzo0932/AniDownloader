import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen, fireEvent } from '@testing-library/svelte';

const { listMock } = vi.hoisted(() => ({ listMock: vi.fn() }));
vi.mock('../api.js', () => ({
  BASE: '',
  posterUrl: (p) => `/poster?path=${p}`,
  posterSrcSet: () => '',
  api: {
    status: async () => ({}),
    download: { status: async () => ({ running: false }) },
    series: { list: (...args) => listMock(...args) },
    config: { get: async () => ({ config: {} }) },
    sse: () => ({ onmessage: null, onerror: null, close() {} }),
  },
}));

import StatusPage from './StatusPage.svelte';

// happy-dom non ha la Web Animations API usata da transition:/rowIn:
// stub con protocollo onfinish (vedi DashboardPage.test.js).
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

// Ordine file: dal piu vecchio al piu recente (come series_data.json).
const seriesFixture = () => [
  { _file_index: 0, name: 'One Piece', path: '/media/one-piece', service: 'animeW_scraper', local_episode_count: 100, last_downloaded_at: '2026-09-20T12:00:00' },
  { _file_index: 1, name: 'Naruto', path: '/media/naruto', service: 'animeU_scraper', local_episode_count: 50, last_downloaded_at: '2026-09-22T12:00:00' },
];

beforeEach(() => {
  document.body.style.overflow = '';
  listMock.mockReset();
  listMock.mockImplementation(async (opts) => {
    const items = seriesFixture();
    if (opts?.sort === 'added' && opts?.dir === 'desc') return { series: [...items].reverse() };
    if (opts?.sort === 'last_downloaded_at') return { series: [...items].reverse() };
    return { series: items };
  });
  vi.stubGlobal('fetch', vi.fn(async () => ({ ok: false })));
});

describe('StatusPage home', () => {
  it('tabelle recenti: nomi + richiesta added desc', async () => {
    render(StatusPage);
    expect(await screen.findAllByText('Naruto')).toHaveLength(2);
    expect(screen.getAllByText('One Piece')).toHaveLength(2);
    expect(listMock).toHaveBeenCalledWith({ sort: 'added', dir: 'desc' });
  });

  it('ultime aggiunte: la piu recente prima', async () => {
    const { container } = render(StatusPage);
    await screen.findAllByText('Naruto');
    const col = container.querySelectorAll('.table-col')[0].textContent;
    expect(col.indexOf('Naruto')).toBeLessThan(col.indexOf('One Piece'));
  });

  it('click riga: apre il dettaglio in sola lettura', async () => {
    const { container } = render(StatusPage);
    await screen.findAllByText('Naruto');
    const rows = container.querySelectorAll('.table-col')[0].querySelectorAll('[role="button"]');
    await fireEvent.click(rows[0]);
    await screen.findByTitle('Torna alle serie');
    // Niente azioni di gestione in home.
    expect(screen.queryByText('Modifica')).toBeNull();
    expect(screen.queryByText('Elimina')).toBeNull();
    expect(document.body.style.overflow).toBe('hidden');
  });
});
