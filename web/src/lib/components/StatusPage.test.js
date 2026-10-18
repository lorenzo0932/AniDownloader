import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen, fireEvent, waitFor } from '@testing-library/svelte';
import { tick } from 'svelte';

const { listMock, statusMock, startMock, sseMock } = vi.hoisted(() => ({
  listMock: vi.fn(), statusMock: vi.fn(), startMock: vi.fn(),
  sseMock: { onopen: null, onmessage: null, onerror: null, close: vi.fn() },
}));
vi.mock('../api.js', () => ({
  BASE: '',
  posterUrl: (p) => `/poster?path=${p}`,
  posterSrcSet: () => '',
  api: {
    status: async () => ({}),
    download: { status: (...args) => statusMock(...args), start: (...args) => startMock(...args), stop: async () => ({}) },
    series: { list: (...args) => listMock(...args) },
    config: { get: async () => ({ config: {} }) },
    sse: () => sseMock,
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
  statusMock.mockReset();
  statusMock.mockResolvedValue({ running: false });
  startMock.mockReset();
  startMock.mockResolvedValue({});
  sseMock.onopen = sseMock.onmessage = sseMock.onerror = null;
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

  it('reload: ricostruisce gli episodi scaricati in attesa senza 99%', async () => {
    statusMock.mockResolvedValue({ running: true, runId: 1, sequence: 4, phase: 'processing', tasks: [
      { key: 'A', seriesName: 'Serie attesa', episode: 4, phase: 'waiting_conversion', percent: -1 },
    ] });
    const { container } = render(StatusPage);
    await screen.findByText('Serie attesa');
    expect(container.querySelector('#home-waiting').textContent).toContain('Serie attesa');
    expect(screen.queryByText('99%')).toBeNull();
    expect(container.querySelector('#home-waiting progress')).toBeNull();
  });

  it('SSE durante snapshot: applica solo gli eventi più nuovi', async () => {
    let resolve;
    statusMock.mockReturnValue(new Promise(r => { resolve = r; }));
    const { container } = render(StatusPage);
    await tick();
    sseMock.onmessage({ data: JSON.stringify({ type: 'progress', runId: 1, sequence: 5,
      task: { key: 'A', seriesName: 'Serie mobile', episode: 2, phase: 'converting', percent: 7 } }) });
    resolve({ running: true, runId: 1, sequence: 4, phase: 'processing', tasks: [
      { key: 'A', seriesName: 'Serie mobile', episode: 2, phase: 'waiting_conversion', percent: -1 },
    ] });
    await screen.findByText('7%');
    expect(container.querySelector('#home-conversion').textContent).toContain('Serie mobile');
    expect(screen.getAllByText('Serie mobile')).toHaveLength(1);
  });

  it('riconnessione: recupera snapshot senza dipendere dal replay SSE', async () => {
    const { container } = render(StatusPage);
    await screen.findAllByText('Naruto');
    statusMock.mockResolvedValue({ running: true, runId: 1, sequence: 8, phase: 'processing', tasks: [
      { key: 'A', seriesName: 'Recuperata', episode: 3, phase: 'converting', percent: 25 },
    ] });
    sseMock.onopen();
    await screen.findByText('25%');
    expect(container.querySelector('#home-conversion').textContent).toContain('Recuperata');
  });

  it('start fallito: errore visibile, nessuna falsa attività', async () => {
    startMock.mockRejectedValue(new Error('Avvio rifiutato'));
    render(StatusPage);
    await screen.findAllByText('Naruto');
    await fireEvent.click(screen.getByRole('button', { name: 'Avvia Download' }));
    await screen.findByRole('alert');
    expect(screen.getByRole('alert').textContent).toContain('Avvio rifiutato');
    expect(screen.getByRole('button', { name: 'Ferma Download' }).disabled).toBe(true);
    await waitFor(() => expect(startMock).toHaveBeenCalledTimes(1));
  });

  it('SSE aperto durante il primo snapshot: risincronizza dopo la sottoscrizione', async () => {
    let resolve;
    statusMock.mockReturnValueOnce(new Promise(r => { resolve = r; }));
    statusMock.mockResolvedValue({ running: true, runId: 1, sequence: 4, phase: 'processing', tasks: [
      { key: 'A', seriesName: 'Tra snapshot e SSE', episode: 2, phase: 'waiting_conversion', percent: -1 },
    ] });
    render(StatusPage);
    await tick();
    sseMock.onopen();
    resolve({ running: true, runId: 1, sequence: 3, phase: 'processing', tasks: [] });
    await screen.findByText('Tra snapshot e SSE');
    expect(statusMock).toHaveBeenCalledTimes(2);
  });
});
