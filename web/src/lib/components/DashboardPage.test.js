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

const seriesFixture = () => [
  { _file_index: 0, name: 'One Piece', path: '/media/one-piece', service: 'animeW_scraper', local_episode_count: 100 },
  { _file_index: 1, name: 'Naruto', path: '/media/naruto', service: 'animeU_scraper', local_episode_count: 50 },
];

beforeEach(() => {
  localStorage.clear();
  listMock.mockReset();
  listMock.mockResolvedValue({ series: seriesFixture() });
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

  it('cambio vista: aggiorna la UI e persiste', async () => {
    const { container } = render(DashboardPage);
    await screen.findByText('One Piece');
    await fireEvent.click(screen.getByTitle('Vista tabella'));
    expect(container.querySelector('.series-table')).toBeTruthy();
    expect(localStorage.getItem(VIEW_KEY)).toBe('table');
    await fireEvent.click(screen.getByTitle('Vista griglia'));
    expect(container.querySelector('.series-grid:not(.grid-large)')).toBeTruthy();
    expect(localStorage.getItem(VIEW_KEY)).toBe('grid');
  });
});
