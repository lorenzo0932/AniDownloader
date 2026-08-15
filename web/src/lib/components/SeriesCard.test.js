import { describe, it, expect, vi } from 'vitest';
import { render, screen, fireEvent } from '@testing-library/svelte';
import SeriesCard from './SeriesCard.svelte';

const itemFixture = (overrides = {}) => ({
  name: 'One Piece',
  path: '/media/one-piece',
  series_page_url: 'https://example.com/one-piece',
  service: 'animeW_scraper',
  local_episode_count: 100,
  ...overrides,
});

describe('SeriesCard', () => {
  it('render: nome, path, url, servizio e conteggio episodi', () => {
    render(SeriesCard, { item: itemFixture(), poster: '/poster.png' });
    expect(screen.getByText('One Piece')).toBeTruthy();
    expect(screen.getByText('/media/one-piece')).toBeTruthy();
    expect(screen.getByText('https://example.com/one-piece')).toBeTruthy();
    expect(screen.getByText('AnimeW')).toBeTruthy();
    expect(screen.getByText('Ep: 100')).toBeTruthy();
  });

  it('render: servizio animeU_scraper mappato a AnimeU', () => {
    render(SeriesCard, { item: itemFixture({ service: 'animeU_scraper' }) });
    expect(screen.getByText('AnimeU')).toBeTruthy();
  });

  it('render: senza service non mostra badge', () => {
    render(SeriesCard, { item: itemFixture({ service: undefined }) });
    expect(screen.queryByText('AnimeW')).toBeNull();
    expect(screen.queryByText('AnimeU')).toBeNull();
  });

  it('render: nome assente usa item.title come fallback', () => {
    render(SeriesCard, { item: itemFixture({ name: undefined, title: 'Titolo Fallback' }) });
    expect(screen.getByText('Titolo Fallback')).toBeTruthy();
  });

  it('click sulla card: invoca onopen', () => {
    const onopen = vi.fn();
    render(SeriesCard, { item: itemFixture(), onopen });
    fireEvent.click(screen.getByRole('button', { name: /One Piece/ }));
    expect(onopen).toHaveBeenCalledTimes(1);
  });

  it('click Modifica: invoca onedit senza onopen', () => {
    const onopen = vi.fn();
    const onedit = vi.fn();
    render(SeriesCard, { item: itemFixture(), onopen, onedit });
    fireEvent.click(screen.getByRole('button', { name: /^Modifica$/ }));
    expect(onedit).toHaveBeenCalledTimes(1);
    expect(onopen).not.toHaveBeenCalled();
  });

  it('click Elimina: invoca onremove senza onopen', () => {
    const onopen = vi.fn();
    const onremove = vi.fn();
    render(SeriesCard, { item: itemFixture(), onopen, onremove });
    fireEvent.click(screen.getByRole('button', { name: /^Elimina$/ }));
    expect(onremove).toHaveBeenCalledTimes(1);
    expect(onopen).not.toHaveBeenCalled();
  });
});
