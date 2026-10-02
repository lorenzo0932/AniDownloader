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
});
