import { describe, it, expect, vi } from 'vitest';
import { render, screen, fireEvent } from '@testing-library/svelte';
import SeriesTable from './SeriesTable.svelte';

const itemsFixture = () => [
  { _file_index: 0, name: 'One Piece', path: '/media/one-piece', service: 'animeW_scraper', local_episode_count: 100 },
  { _file_index: 1, name: 'Naruto', path: '/media/naruto', service: 'animeU_scraper', local_episode_count: 50 },
];

describe('SeriesTable', () => {
  it('render: una riga per serie con nome, servizio ed episodi', () => {
    render(SeriesTable, { items: itemsFixture() });
    expect(screen.getByText('One Piece')).toBeTruthy();
    expect(screen.getByText('Naruto')).toBeTruthy();
    expect(screen.getByText('AnimeW')).toBeTruthy();
    expect(screen.getByText('AnimeU')).toBeTruthy();
  });

  it('render: miniature via /api/poster w=96', () => {
    const { container } = render(SeriesTable, { items: itemsFixture() });
    // alt="" => presentazionali, fuori dall'accessibility tree: query sul DOM.
    const imgs = container.querySelectorAll('img.row-poster');
    expect(imgs).toHaveLength(2);
    expect(imgs[0].getAttribute('src')).toContain('/api/poster');
    expect(imgs[0].getAttribute('src')).toContain('w=96');
  });

  it('click sulla riga: invoca onopen con _file_index', () => {
    const onopen = vi.fn();
    render(SeriesTable, { items: itemsFixture(), onopen });
    fireEvent.click(screen.getByText('Naruto'));
    expect(onopen).toHaveBeenCalledTimes(1);
    expect(onopen).toHaveBeenCalledWith(1);
  });

  it('Modifica: invoca onedit con _file_index senza onopen', () => {
    const onopen = vi.fn();
    const onedit = vi.fn();
    render(SeriesTable, { items: itemsFixture(), onopen, onedit });
    fireEvent.click(screen.getAllByRole('button', { name: 'Modifica' })[0]);
    expect(onedit).toHaveBeenCalledWith(0);
    expect(onopen).not.toHaveBeenCalled();
  });

  it('Elimina: invoca onremove con _file_index senza onopen', () => {
    const onopen = vi.fn();
    const onremove = vi.fn();
    render(SeriesTable, { items: itemsFixture(), onopen, onremove });
    fireEvent.click(screen.getAllByRole('button', { name: 'Elimina' })[1]);
    expect(onremove).toHaveBeenCalledWith(1);
    expect(onopen).not.toHaveBeenCalled();
  });

  it('riga gestionale: ultimo download in forma breve', () => {
    const items = itemsFixture();
    items[0].last_downloaded_at = '2026-09-22T12:00:00';
    const { container } = render(SeriesTable, { items });
    // Formato it-IT gg/mm/aaaa, robusto al fuso (mezzogiorno UTC).
    const metas = container.querySelectorAll('.row-meta');
    expect(metas[0].textContent).toMatch(/\d{2}\/\d{2}\/\d{4}/);
    expect(metas[1].textContent).not.toMatch(/\d{2}\/\d{2}\/\d{4}/);
  });

  it('riga gestionale: data assente o invalida -> nascosta', () => {
    const items = itemsFixture();
    items[0].last_downloaded_at = '';
    items[1].last_downloaded_at = 'non-una-data';
    const { container } = render(SeriesTable, { items });
    const metas = container.querySelectorAll('.row-meta');
    for (const m of metas) expect(m.textContent).not.toMatch(/\d{2}\/\d{2}\/\d{4}/);
    // Episodi restano sempre visibili.
    expect(metas[0].textContent).toContain('Ep: 100');
  });

  it('riga gestionale: badge Alta Priorita solo se flaggata', () => {
    const items = itemsFixture();
    items[0].is_high_priority = true;
    render(SeriesTable, { items });
    expect(screen.getAllByText('Alta Priorità')).toHaveLength(1);
  });
});
