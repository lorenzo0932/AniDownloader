import { describe, it, expect, vi } from 'vitest';
import { render, screen } from '@testing-library/svelte';
import SeriesList from './SeriesList.svelte';

const itemsFixture = () => [
  { _file_index: 0, name: 'One Piece', path: '/media/one-piece', service: 'animeW_scraper', local_episode_count: 100 },
  { _file_index: 1, name: 'Naruto', path: '/media/naruto', service: 'animeU_scraper', local_episode_count: 50 },
];

const noop = () => {};

describe('SeriesList', () => {
  it('vuota: messaggio dedicato', () => {
    render(SeriesList, { items: [], totalCount: 0, onopen: noop, onedit: noop, onremove: noop });
    expect(screen.getByText(/Nessuna serie configurata/)).toBeTruthy();
  });

  it('grid (default): card con poster, niente righe tabella', () => {
    const { container } = render(SeriesList, {
      items: itemsFixture(), viewMode: 'grid', totalCount: 2, onopen: noop, onedit: noop, onremove: noop,
    });
    expect(container.querySelector('.series-grid')).toBeTruthy();
    expect(container.querySelector('.series-table')).toBeNull();
    expect(screen.getByText('One Piece')).toBeTruthy();
  });

  it('table: righe dense, niente griglia card', () => {
    const { container } = render(SeriesList, {
      items: itemsFixture(), viewMode: 'table', totalCount: 2, onopen: noop, onedit: noop, onremove: noop,
    });
    expect(container.querySelector('.series-table')).toBeTruthy();
    expect(container.querySelector('.series-grid')).toBeNull();
    expect(screen.getByText('Naruto')).toBeTruthy();
  });

  it('large: griglia con colonne grandi, niente tabella', () => {
    const { container } = render(SeriesList, {
      items: itemsFixture(), viewMode: 'large', totalCount: 2, onopen: noop, onedit: noop, onremove: noop,
    });
    expect(container.querySelector('.series-grid.grid-large')).toBeTruthy();
    expect(container.querySelector('.series-table')).toBeNull();
    expect(screen.getByText('One Piece')).toBeTruthy();
  });

  it('cambio vista grid -> large: applica grid-large (regressione reattivita)', async () => {
    const base = { items: itemsFixture(), totalCount: 2, onopen: noop, onedit: noop, onremove: noop };
    const { container, rerender } = render(SeriesList, { ...base, viewMode: 'grid' });
    expect(container.querySelector('.series-grid:not(.grid-large)')).toBeTruthy();
    await rerender({ ...base, viewMode: 'large' });
    expect(container.querySelector('.series-grid.grid-large')).toBeTruthy();
  });

  it('cambio vista grid -> table: mostra la tabella (regressione reattivita)', async () => {
    const base = { items: itemsFixture(), totalCount: 2, onopen: noop, onedit: noop, onremove: noop };
    const { container, rerender } = render(SeriesList, { ...base, viewMode: 'grid' });
    expect(container.querySelector('.series-grid')).toBeTruthy();
    await rerender({ ...base, viewMode: 'table' });
    expect(container.querySelector('.series-table')).toBeTruthy();
    expect(container.querySelector('.series-grid')).toBeNull();
  });
});
