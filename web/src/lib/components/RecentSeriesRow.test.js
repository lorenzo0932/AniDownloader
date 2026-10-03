import { describe, it, expect, vi } from 'vitest';
import { render, screen, fireEvent } from '@testing-library/svelte';
import RecentSeriesRow from './RecentSeriesRow.svelte';

const itemFixture = () => ({
  _file_index: 3,
  name: 'One Piece',
  path: '/media/one-piece',
  service: 'animeU_scraper',
});

describe('RecentSeriesRow', () => {
  it('render: miniatura, nome e servizio', () => {
    const { container } = render(RecentSeriesRow, { item: itemFixture(), onopen: () => {} });
    expect(screen.getByText('One Piece')).toBeTruthy();
    expect(screen.getByText('AnimeU')).toBeTruthy();
    const img = container.querySelector('img.home-poster');
    expect(img.getAttribute('src')).toContain('/api/poster');
    expect(img.getAttribute('src')).toContain('w=96');
  });

  it('click sulla riga: invoca onopen con _file_index', async () => {
    const onopen = vi.fn();
    render(RecentSeriesRow, { item: itemFixture(), onopen });
    await fireEvent.click(screen.getByRole('button', { name: 'One Piece' }));
    expect(onopen).toHaveBeenCalledTimes(1);
    expect(onopen).toHaveBeenCalledWith(3);
  });

  it('Invio da tastiera: invoca onopen', async () => {
    const onopen = vi.fn();
    render(RecentSeriesRow, { item: itemFixture(), onopen });
    await fireEvent.keyDown(screen.getByRole('button', { name: 'One Piece' }), { key: 'Enter' });
    expect(onopen).toHaveBeenCalledWith(3);
  });
});
