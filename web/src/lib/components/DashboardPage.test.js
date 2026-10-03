import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen, fireEvent } from '@testing-library/svelte';

const { listMock } = vi.hoisted(() => ({ listMock: vi.fn() }));
vi.mock('../api.js', () => ({
  BASE: '',
  posterUrl: () => '/poster',
  posterSrcSet: () => '',
  api: { series: { list: (...args) => listMock(...args) } },
}));

import DashboardPage from './DashboardPage.svelte';

// happy-dom non ha la Web Animations API usata da `in:fly`: stub minimo.
// Svelte usa solo animate() -> { finished, cancel(), onfinish }.
if (typeof Element !== 'undefined' && !Element.prototype.animate) {
  Element.prototype.animate = function () {
    return {
      finished: Promise.resolve(),
      cancel() {}, play() {}, pause() {}, reverse() {},
      onfinish: null, currentTime: 0,
    };
  };
}

const VIEW_KEY = 'anidl.series.view';
const SORT_KEY = 'anidl.series.sort';

const seriesFixture = () => [
  { _file_index: 0, name: 'One Piece', path: '/media/one-piece', service: 'animeW_scraper', local_episode_count: 100 },
  { _file_index: 1, name: 'Naruto', path: '/media/naruto', service: 'animeU_scraper', local_episode_count: 50 },
];

beforeEach(() => {
  localStorage.clear();
  document.body.style.overflow = '';
  listMock.mockReset();
  // Array fresco a ogni chiamata: load() fa reverse() in place per
  // 'added'+desc e non deve inquinare i load successivi.
  listMock.mockImplementation(async () => ({ series: seriesFixture() }));
  vi.stubGlobal('fetch', vi.fn(async () => ({ ok: false })));
});

describe('DashboardPage vista', () => {
  it('default: vista large (griglia con colonne grandi)', async () => {
    const { container } = render(DashboardPage);
    await screen.findByText('One Piece');
    expect(container.querySelector('.series-grid.grid-large')).toBeTruthy();
    expect(container.querySelector('.series-table')).toBeNull();
  });

  it('default: vista persistita al mount', async () => {
    render(DashboardPage);
    await screen.findByText('One Piece');
    expect(localStorage.getItem(VIEW_KEY)).toBe('large');
  });

  it('legge la vista da localStorage (tabella)', async () => {
    localStorage.setItem(VIEW_KEY, 'table');
    const { container } = render(DashboardPage);
    await screen.findByText('Naruto');
    expect(container.querySelector('.series-table')).toBeTruthy();
    expect(container.querySelector('.series-grid')).toBeNull();
  });

  it('valore storage non valido: fallback a large', async () => {
    localStorage.setItem(VIEW_KEY, 'masonry');
    const { container } = render(DashboardPage);
    await screen.findByText('One Piece');
    expect(container.querySelector('.series-grid.grid-large')).toBeTruthy();
  });

  it('cambio vista: aggiorna la UI e persiste', async () => {    const { container } = render(DashboardPage);
    await screen.findByText('One Piece');
    await fireEvent.click(screen.getByTitle('Vista tabella'));
    expect(container.querySelector('.series-table')).toBeTruthy();
    expect(localStorage.getItem(VIEW_KEY)).toBe('table');
    await fireEvent.click(screen.getByTitle('Vista griglia'));
    expect(container.querySelector('.series-grid:not(.grid-large)')).toBeTruthy();
    expect(localStorage.getItem(VIEW_KEY)).toBe('grid');
  });

  it('bottone large: hook CSS per nasconderlo su telefono', async () => {
    // Il nascondiglio e media-query (non verificabile in happy-dom):
    // qui si fissa solo l'hook che il CSS usa.
    render(DashboardPage);
    await screen.findByText('One Piece');
    expect(screen.getByTitle('Vista grande').classList.contains('view-btn-large')).toBe(true);
  });
});

describe('DashboardPage ordinamento', () => {
  it('default: data inserimento desc (piu recenti prima), senza sort server', async () => {
    const { container } = render(DashboardPage);
    await screen.findByText('One Piece');
    // 'added' = ordine file senza parametri server...
    expect(listMock).toHaveBeenCalledTimes(1);
    expect(listMock).toHaveBeenCalledWith({});
    // ...invertito lato client (desc): Naruto (indice 1) prima di One Piece.
    const gridText = container.querySelector('.series-grid').textContent;
    expect(gridText.indexOf('Naruto')).toBeLessThan(gridText.indexOf('One Piece'));
  });

  it('legge l ordinamento da localStorage (nome asc)', async () => {
    localStorage.setItem(SORT_KEY, JSON.stringify({ field: 'name', dir: 'asc' }));
    const { container } = render(DashboardPage);
    await screen.findByText('One Piece');
    expect(listMock).toHaveBeenCalledWith({ sort: 'name', dir: 'asc' });
    const gridText = container.querySelector('.series-grid').textContent;
    expect(gridText.indexOf('One Piece')).toBeLessThan(gridText.indexOf('Naruto'));
  });

  it('storage non valido: fallback a data-desc', async () => {
    localStorage.setItem(SORT_KEY, '{rotto');
    render(DashboardPage);
    await screen.findByText('One Piece');
    expect(listMock).toHaveBeenCalledWith({});
  });

  it('toggle direzione: ricarica e persiste', async () => {
    const { container } = render(DashboardPage);
    await screen.findByText('One Piece');
    // Default desc -> click -> asc: niente reverse, torna ordine file.
    await fireEvent.click(screen.getByTitle('Decrescente'));
    expect(JSON.parse(localStorage.getItem(SORT_KEY))).toEqual({ field: 'added', dir: 'asc' });
    // Il reload e ri-render sono asincroni: attendi il secondo load
    // e poi il nuovo ordine nel DOM.
    await vi.waitFor(() => expect(listMock).toHaveBeenCalledTimes(2));
    await vi.waitFor(() => {
      const gridText = container.querySelector('.series-grid').textContent;
      expect(gridText.indexOf('One Piece')).toBeLessThan(gridText.indexOf('Naruto'));
    });
  });
});

describe('DashboardPage modali', () => {
  it('apertura dettaglio: lock scroll sfondo; chiusura: ripristino', async () => {
    render(DashboardPage);
    await screen.findByText('One Piece');
    expect(document.body.style.overflow).toBe('');
    // Le card hanno role=button con aria-label = nome serie.
    await fireEvent.click(screen.getByRole('button', { name: 'One Piece' }));
    await screen.findByTitle('Torna alle serie');
    expect(document.body.style.overflow).toBe('hidden');
    await fireEvent.click(screen.getByTitle('Torna alle serie'));
    await vi.waitFor(() => expect(document.body.style.overflow).toBe(''));
  });
});
