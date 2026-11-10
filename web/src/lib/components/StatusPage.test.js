import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen, fireEvent } from '@testing-library/svelte';
import { tick } from 'svelte';

const { listMock, statusMock, startMock, sseMock, overviewMock } = vi.hoisted(() => ({
  listMock: vi.fn(), statusMock: vi.fn(), startMock: vi.fn(),
  sseMock: { onopen: null, onmessage: null, onerror: null, close: vi.fn() },
  overviewMock: vi.fn(),
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
    runs: { overview: (...args) => overviewMock(...args) },
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

const emptyOverview = () => ({
  run: null,
  coda: { queued: 0, downloading: 0, converting: 0, retry: 0, totale: 0 },
  problemi: { non_riusciti: 0, interrotti: 0 },
});

const runningOverview = (over = {}, extra = {}) => ({
  run: {
    id: 'run-1', origine: 'manuale',
    avviato_il: '2026-11-08T17:03:00.000Z', concluso_il: null,
    stato: 'in_corso', totale: 7, completati: 3, non_riusciti: 1,
    saltati: 0, in_corso: 3, percent: 57,
    corrente: { serie: 'Psyren', episodio: 4, fase: 'converting' },
    ...over,
  },
  coda: { queued: 2, downloading: 1, converting: 1, retry: 0, totale: 4 },
  problemi: { non_riusciti: 1, interrotti: 0 },
  ...extra,
});

beforeEach(() => {
  document.body.style.overflow = '';
  listMock.mockReset();
  statusMock.mockReset();
  statusMock.mockResolvedValue({ running: false });
  startMock.mockReset();
  startMock.mockResolvedValue({});
  sseMock.onopen = sseMock.onmessage = sseMock.onerror = null;
  overviewMock.mockReset();
  overviewMock.mockResolvedValue(emptyOverview());
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

  it('home senza run: empty state della card e nessuna board di fase', async () => {
    const { container } = render(StatusPage);
    expect(await screen.findByText(/Nessun avvio registrato/)).toBeTruthy();
    expect(overviewMock).toHaveBeenCalled();
    expect(container.querySelector('.phase-board')).toBeNull();
    expect(container.querySelector('.pipeline-summary')).toBeNull();
    expect(screen.queryByRole('link', { name: /in coda/ })).toBeNull();
    expect(screen.queryByRole('link', { name: /problema/ })).toBeNull();
  });

  it('run in corso: card con barra determinata, corrente, conteggi e CTA', async () => {
    overviewMock.mockResolvedValue(runningOverview());
    const { container } = render(StatusPage);

    expect(await screen.findByText('Avvio corrente')).toBeTruthy();
    const bar = screen.getByRole('progressbar');
    expect(bar.getAttribute('aria-valuenow')).toBe('57');
    expect(bar.getAttribute('aria-valuetext')).toBe('3 di 7 episodi');
    expect(screen.getByText('Psyren · Ep. 4 · In conversione')).toBeTruthy();
    expect(screen.getByText('3 completati')).toBeTruthy();
    expect(screen.getByText('3 in corso')).toBeTruthy();
    expect(screen.getByText('1 non riuscito')).toBeTruthy();
    expect(screen.getByRole('link', { name: 'Apri attività' })).toBeTruthy();
    expect(screen.getByRole('link', { name: '4 in coda' })).toBeTruthy();
    expect(screen.getByRole('link', { name: '1 problema' })).toBeTruthy();
    expect(container.querySelector('.phase-board')).toBeNull();
    expect(container.querySelector('.pipeline-summary')).toBeNull();
  });

  it('stato globale: chip coda e problemi sotto la card', async () => {
    overviewMock.mockResolvedValue(runningOverview({}, {
      coda: { queued: 2, downloading: 3, converting: 1, retry: 4, totale: 10 },
      problemi: { non_riusciti: 2, interrotti: 1 },
    }));
    render(StatusPage);

    const coda = await screen.findByRole('link', { name: '10 in coda' });
    expect(coda.getAttribute('href')).toBe('/attivita');
    const problemi = screen.getByRole('link', { name: '3 problemi' });
    expect(problemi.getAttribute('href')).toBe('/attivita?tab=problemi');

    const nav = vi.fn();
    window.addEventListener('anidl:navigate', nav);
    try {
      await fireEvent.click(coda);
      expect(nav).toHaveBeenCalledTimes(1);
      expect(nav.mock.calls[0][0].detail).toBe('attivita');
    } finally {
      window.removeEventListener('anidl:navigate', nav);
    }
  });

  it('stato globale nascosto quando coda e problemi sono a zero', async () => {
    overviewMock.mockResolvedValue(runningOverview({}, {
      coda: { queued: 0, downloading: 0, converting: 0, retry: 0, totale: 0 },
      problemi: { non_riusciti: 0, interrotti: 0 },
    }));
    const { container } = render(StatusPage);

    expect(await screen.findByText('Avvio corrente')).toBeTruthy();
    expect(screen.queryByRole('link', { name: /in coda/ })).toBeNull();
    expect(screen.queryByRole('link', { name: /problema/ })).toBeNull();
  });

  it('run attivo: task live con percentuali e stato globale derivato', async () => {
    statusMock.mockResolvedValue({
      running: true,
      tasks: [
        { key: 'Witch#6', seriesName: 'Witch', episode: 6, phase: 'converting', percent: 70 },
        { key: 'Psyren#5', seriesName: 'Psyren', episode: 5, phase: 'downloading', percent: 36 },
      ],
    });
    overviewMock.mockResolvedValue(runningOverview({
      totale: 2, completati: 0, in_corso: 2, percent: 0,
      corrente: { serie: 'Witch', episodio: 6, fase: 'converting' },
    }));
    render(StatusPage);

    expect(await screen.findByText('Avvio corrente')).toBeTruthy();
    expect(await screen.findByText('1 in download · 1 in conversione')).toBeTruthy();
    const bars = screen.getAllByRole('progressbar');
    expect(bars[0].getAttribute('aria-valuenow')).toBe('53');
    expect(screen.getByText('70%')).toBeTruthy();
    expect(screen.getByText('36%')).toBeTruthy();
    expect(screen.getByText('Witch')).toBeTruthy();
    expect(screen.getByText('Psyren')).toBeTruthy();
  });

  it('run automatico: task live da overview senza run manuale', async () => {
    statusMock.mockResolvedValue({ running: false });
    overviewMock.mockResolvedValue(runningOverview({
      origine: 'automatico', totale: 2, completati: 0, in_corso: 2, percent: 0,
      corrente: { serie: 'Witch', episodio: 6, fase: 'converting' },
      tasks: [
        { key: '["Witch",6]', seriesName: 'Witch', episode: 6, phase: 'converting', percent: 60 },
        { key: '["Psyren",5]', seriesName: 'Psyren', episode: 5, phase: 'downloading', percent: 30 },
      ],
    }));
    render(StatusPage);

    expect(await screen.findByText('Avvio corrente')).toBeTruthy();
    expect(await screen.findByText('1 in download · 1 in conversione')).toBeTruthy();
    const bars = screen.getAllByRole('progressbar');
    expect(bars[0].getAttribute('aria-valuenow')).toBe('45');
    expect(screen.getByText('60%')).toBeTruthy();
    expect(screen.getByText('30%')).toBeTruthy();
    expect(screen.getByText('Witch')).toBeTruthy();
    expect(screen.getByText('Psyren')).toBeTruthy();
    // Nessun pulsante Stop: il check automatico non è interrompibile da lì.
    expect(screen.queryByRole('button', { name: 'Ferma Download' })).toBeNull();
  });

  it('run completato: ultimo avvio e CTA dettaglio', async () => {
    overviewMock.mockResolvedValue(runningOverview({
      stato: 'completato', concluso_il: '2026-11-08T17:10:00.000Z',
      completati: 7, non_riusciti: 0, in_corso: 0, percent: 100, corrente: null,
    }));
    render(StatusPage);

    expect(await screen.findByText('Ultimo avvio')).toBeTruthy();
    expect(screen.queryByRole('progressbar')).toBeNull();
    expect(screen.getByText('7 completati')).toBeTruthy();
    expect(screen.getByRole('link', { name: 'Vedi dettaglio' })).toBeTruthy();
  });

  it('run con errori e origine automatica: badge e non riusciti', async () => {
    overviewMock.mockResolvedValue(runningOverview({
      origine: 'automatico', stato: 'completato_con_errori',
      concluso_il: '2026-11-08T17:10:00.000Z',
      completati: 5, non_riusciti: 2, in_corso: 0, percent: 100, corrente: null,
    }));
    render(StatusPage);

    expect(await screen.findByText('Completato con errori')).toBeTruthy();
    expect(screen.getByText('automatico')).toBeTruthy();
    expect(screen.getByText('2 non riusciti')).toBeTruthy();
  });

  it('polling overview ogni 3s non cambia il pulsante di avvio', async () => {
    vi.useFakeTimers();
    try {
      render(StatusPage);
      await vi.advanceTimersByTimeAsync(0);
      await tick();
      expect(overviewMock).toHaveBeenCalledTimes(1);
      expect(screen.getByRole('button', { name: 'Avvia Download' })).toBeTruthy();

      await vi.advanceTimersByTimeAsync(3000);
      await tick();
      expect(overviewMock).toHaveBeenCalledTimes(2);
      expect(screen.getByRole('button', { name: 'Avvia Download' })).toBeTruthy();
      expect(screen.queryByRole('button', { name: 'Ferma Download' })).toBeNull();
    } finally {
      vi.useRealTimers();
    }
  });

  it('SSE progress: ricarica overview con debounce', async () => {
    vi.useFakeTimers();
    try {
      render(StatusPage);
      await vi.advanceTimersByTimeAsync(0);
      await tick();
      expect(overviewMock).toHaveBeenCalledTimes(1);

      sseMock.onmessage({ data: JSON.stringify({ type: 'progress', runId: 1, sequence: 1,
        task: { key: 'A', seriesName: 'Serie mobile', episode: 2, phase: 'converting', percent: 7 } }) });
      await vi.advanceTimersByTimeAsync(300);
      expect(overviewMock).toHaveBeenCalledTimes(1);

      await vi.advanceTimersByTimeAsync(400);
      expect(overviewMock).toHaveBeenCalledTimes(2);
    } finally {
      vi.useRealTimers();
    }
  });

  it('start fallito: errore visibile, nessuna falsa attività', async () => {
    startMock.mockRejectedValue(new Error('Avvio rifiutato'));
    render(StatusPage);
    await screen.findAllByText('Naruto');
    await fireEvent.click(screen.getByRole('button', { name: 'Avvia Download' }));
    await screen.findByRole('alert');
    expect(screen.getByRole('alert').textContent).toContain('Avvio rifiutato');
    expect(screen.queryByRole('button', { name: 'Ferma Download' })).toBeNull();
    await screen.findByText(/Nessun avvio registrato/);
  });
});
