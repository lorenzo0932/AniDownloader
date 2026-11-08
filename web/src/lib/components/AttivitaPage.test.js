import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { render, screen, fireEvent, waitFor, cleanup } from '@testing-library/svelte';
import AttivitaPage from './AttivitaPage.svelte';

// happy-dom non ha la Web Animations API usata dalle transizioni Svelte.
if (typeof Element !== 'undefined' && !Element.prototype.animate) {
  Element.prototype.animate = function () {
    const anim = {
      currentTime: 0,
      playState: 'finished',
      onfinish: null,
      cancel() {}, play() {}, pause() {}, reverse() {},
      finished: null,
    };
    anim.finished = Promise.resolve().then(() => { anim.onfinish?.(); });
    return anim;
  };
}

const {
  runsListMock, runsGetMock, runsRetryFailedMock,
  pipelineListMock, pipelineRetryMock,
} = vi.hoisted(() => ({
  runsListMock: vi.fn(),
  runsGetMock: vi.fn(),
  runsRetryFailedMock: vi.fn(),
  pipelineListMock: vi.fn(),
  pipelineRetryMock: vi.fn(),
}));

vi.mock('../api.js', () => ({
  api: {
    runs: {
      list: runsListMock,
      get: runsGetMock,
      retryFailed: runsRetryFailedMock,
    },
    pipeline: {
      list: pipelineListMock,
      retry: pipelineRetryMock,
    },
  },
  posterUrl: (path, w = 480) => '/poster?w=' + w + '&path=' + encodeURIComponent(path),
  posterSrcSet: (path) => '/poster?w=96&path=' + encodeURIComponent(path) + ' 96w',
}));

const run = (over = {}) => ({
  id: 'run-1',
  origine: 'manuale',
  avviato_il: new Date(Date.now() - 5 * 60 * 1000).toISOString(),
  concluso_il: null,
  stato: 'in_corso',
  totale: 4,
  completati: 1,
  non_riusciti: 1,
  saltati: 0,
  in_corso: 2,
  percent: 50,
  ...over,
});

const job = (over = {}) => ({
  id: 101,
  run_id: 'run-1',
  job_key: 'animeW_scraper:/play/x#1',
  path: '/media/Serie X',
  serie: 'Serie X',
  episodio: 1,
  origine: 'interactive',
  fase: 'queued_download',
  tentativi: 0,
  prossima_esecuzione: 0,
  priorita: 100,
  errore: '',
  creato_il: new Date().toISOString(),
  aggiornato_il: new Date().toISOString(),
  ...over,
});

const listaAvvii = () => ({
  items: [run()],
  total: 1,
  counts: { in_corso: 1, recenti: 1, problemi: 1 },
});

const listaEpisodi = () => ({ items: [job()], counts: {} });

beforeEach(() => {
  vi.clearAllMocks();
  window.history.replaceState({}, '', '/attivita');
  runsListMock.mockResolvedValue(listaAvvii());
  runsGetMock.mockResolvedValue({ run: run(), jobs: [job()] });
  runsRetryFailedMock.mockResolvedValue({ success: true, retried: 1 });
  pipelineListMock.mockResolvedValue(listaEpisodi());
  pipelineRetryMock.mockResolvedValue({ success: true, id: 101 });
});

afterEach(() => {
  window.history.replaceState({}, '', '/attivita');
});

describe('AttivitaPage', () => {
  it('default: vista Avvii, tab Recenti, finestra recenti via api.runs.list', async () => {
    render(AttivitaPage);
    expect(await screen.findByText('Avvio manuale')).toBeTruthy();

    expect(runsListMock).toHaveBeenCalledWith({
      window: 'recenti', origine: 'tutte', stato: 'tutti', limit: 20, offset: 0,
    });
    const recenti = screen.getByRole('tab', { name: 'Recenti' });
    expect(recenti.getAttribute('aria-selected')).toBe('true');
    expect(recenti.classList.contains('active')).toBe(true);
    expect(screen.getByRole('tab', { name: 'Storico' }).getAttribute('aria-selected'))
      .toBe('false');
    expect(screen.getByRole('button', { name: 'Avvii' }).getAttribute('aria-pressed'))
      .toBe('true');
    expect(screen.getByRole('button', { name: 'Episodi' }).getAttribute('aria-pressed'))
      .toBe('false');
  });

  it('deep link ?tab=problemi: apre Avvii/Problemi', async () => {
    window.history.replaceState({}, '', '/attivita?tab=problemi');
    render(AttivitaPage);
    expect(await screen.findByText('Avvio manuale')).toBeTruthy();
    expect(runsListMock).toHaveBeenCalledWith({
      window: 'tutti', origine: 'tutte', stato: 'problemi', limit: 20, offset: 0,
    });
    expect(screen.getByRole('button', { name: 'Avvii' }).getAttribute('aria-pressed'))
      .toBe('true');
    expect(screen.getByRole('tab', { name: /^Problemi/ }).getAttribute('aria-selected'))
      .toBe('true');
  });

  it('deep link ?vista=episodi: apre Episodi/Recenti', async () => {
    window.history.replaceState({}, '', '/attivita?vista=episodi');
    render(AttivitaPage);
    expect(await screen.findByText('Serie X · Ep. 1')).toBeTruthy();
    expect(pipelineListMock).toHaveBeenCalledWith({
      window: 'recenti', limit: 50, offset: 0,
    });
    expect(screen.getByRole('button', { name: 'Episodi' }).getAttribute('aria-pressed'))
      .toBe('true');
    expect(screen.getByRole('tab', { name: 'Recenti' }).getAttribute('aria-selected'))
      .toBe('true');
  });

  it('deep link con parametri invalidi: resta su Avvii/Recenti', async () => {
    window.history.replaceState({}, '', '/attivita?tab=boh&vista=altro');
    render(AttivitaPage);
    expect(await screen.findByText('Avvio manuale')).toBeTruthy();
    expect(runsListMock).toHaveBeenCalledWith({
      window: 'recenti', origine: 'tutte', stato: 'tutti', limit: 20, offset: 0,
    });
    expect(pipelineListMock).not.toHaveBeenCalled();
    expect(screen.getByRole('tab', { name: 'Recenti' }).getAttribute('aria-selected'))
      .toBe('true');
    expect(screen.getByRole('button', { name: 'Avvii' }).getAttribute('aria-pressed'))
      .toBe('true');
  });

  it('tab Problemi: window tutti, stato problemi e badge conteggio', async () => {
    render(AttivitaPage);
    await screen.findByText('Avvio manuale');
    expect(screen.getByRole('tab', { name: 'Problemi (1)' })).toBeTruthy();
    await fireEvent.click(screen.getByRole('tab', { name: /^Problemi/ }));
    await waitFor(() => expect(runsListMock).toHaveBeenLastCalledWith({
      window: 'tutti', origine: 'tutte', stato: 'problemi', limit: 20, offset: 0,
    }));
    expect(screen.getByRole('tab', { name: /^Problemi/ }).getAttribute('aria-selected'))
      .toBe('true');
  });

  it('tab Storico: window storico', async () => {
    render(AttivitaPage);
    await screen.findByText('Avvio manuale');
    await fireEvent.click(screen.getByRole('tab', { name: 'Storico' }));
    await waitFor(() => expect(runsListMock).toHaveBeenLastCalledWith({
      window: 'storico', origine: 'tutte', stato: 'tutti', limit: 20, offset: 0,
    }));
  });

  it('filtro origine: chiama api.runs.list con manuale e resetta offset', async () => {
    render(AttivitaPage);
    await screen.findByText('Avvio manuale');
    await fireEvent.click(screen.getByRole('button', { name: 'Manuale' }));
    await waitFor(() => expect(runsListMock).toHaveBeenLastCalledWith({
      window: 'recenti', origine: 'manuale', stato: 'tutti', limit: 20, offset: 0,
    }));
    const manuale = screen.getByRole('button', { name: 'Manuale' });
    expect(manuale.getAttribute('aria-pressed')).toBe('true');
  });

  it('vista Episodi: usa api.pipeline.list con limit 50 e senza filtro origine', async () => {
    render(AttivitaPage);
    await screen.findByText('Avvio manuale');
    await fireEvent.click(screen.getByRole('button', { name: 'Episodi' }));
    await waitFor(() => expect(pipelineListMock).toHaveBeenLastCalledWith({
      window: 'recenti', limit: 50, offset: 0,
    }));
    expect(screen.queryByRole('button', { name: 'Manuale' })).toBeNull();
    expect(await screen.findByText('Serie X · Ep. 1')).toBeTruthy();
    expect(screen.getByText('In coda')).toBeTruthy();
  });

  it('Episodi + Problemi: fase problemi e window tutti', async () => {
    render(AttivitaPage);
    await screen.findByText('Avvio manuale');
    await fireEvent.click(screen.getByRole('button', { name: 'Episodi' }));
    await waitFor(() => expect(pipelineListMock).toHaveBeenCalled());
    await fireEvent.click(screen.getByRole('tab', { name: 'Problemi' }));
    await waitFor(() => expect(pipelineListMock).toHaveBeenLastCalledWith({
      window: 'tutti', fase: 'problemi', limit: 50, offset: 0,
    }));
    // In Episodi niente badge: il nome della tab è esattamente "Problemi".
    expect(screen.getByRole('tab', { name: 'Problemi' })).toBeTruthy();
  });

  it('deep link ?run=<id>: priorità su tab/vista, apre Avvii/Recenti ed espande il run', async () => {
    window.history.replaceState({}, '', '/attivita?run=run-9&tab=storico&vista=episodi');
    runsGetMock.mockResolvedValue({
      run: run({ id: 'run-9' }),
      jobs: [job({ id: 909, run_id: 'run-9', serie: 'Serie Deep', episodio: 7 })],
    });
    render(AttivitaPage);
    expect(await screen.findByText('Serie Deep · Ep. 7')).toBeTruthy();
    expect(runsGetMock).toHaveBeenCalledWith('run-9');
    expect(runsListMock).toHaveBeenCalledWith({
      window: 'recenti', origine: 'tutte', stato: 'tutti', limit: 20, offset: 0,
    });
    expect(pipelineListMock).not.toHaveBeenCalled();
    expect(screen.getByRole('tab', { name: 'Recenti' }).getAttribute('aria-selected'))
      .toBe('true');
    const riga = document.querySelector('[data-run-id="run-9"] .run-main');
    expect(riga.getAttribute('aria-expanded')).toBe('true');
  });

  it('paginazione: Carica altri usa offset e appende; il cambio tab resetta', async () => {
    const prima = Array.from({ length: 20 }, (_, i) => run({
      id: `run-${i}`, totale: 1, completati: 1, non_riusciti: 0,
      in_corso: 0, percent: 100, stato: 'completato',
      concluso_il: new Date().toISOString(),
    }));
    const dopo = Array.from({ length: 5 }, (_, i) => run({
      id: `run-${20 + i}`, totale: 1, completati: 1, non_riusciti: 0,
      in_corso: 0, percent: 100, stato: 'completato',
      concluso_il: new Date().toISOString(),
    }));
    runsListMock
      .mockResolvedValueOnce({ items: prima, total: 25, counts: { in_corso: 0, recenti: 25, problemi: 0 } })
      .mockResolvedValueOnce({ items: dopo, total: 25, counts: { in_corso: 0, recenti: 25, problemi: 0 } });

    render(AttivitaPage);
    await waitFor(() => expect(document.querySelectorAll('.run').length).toBe(20));
    await fireEvent.click(screen.getByRole('button', { name: 'Carica altri' }));
    await waitFor(() => expect(runsListMock).toHaveBeenLastCalledWith({
      window: 'recenti', origine: 'tutte', stato: 'tutti', limit: 20, offset: 20,
    }));
    await waitFor(() => expect(document.querySelectorAll('.run').length).toBe(25));

    await fireEvent.click(screen.getByRole('tab', { name: 'Storico' }));
    await waitFor(() => expect(runsListMock).toHaveBeenLastCalledWith({
      window: 'storico', origine: 'tutte', stato: 'tutti', limit: 20, offset: 0,
    }));
    await waitFor(() => expect(document.querySelectorAll('.run').length).toBe(1));
  });

  it('"Riprova tutti" chiama retryFailed e ricarica', async () => {
    render(AttivitaPage);
    await screen.findByText('Avvio manuale');
    await fireEvent.click(screen.getByRole('button', {
      name: 'Riprova tutti i non riusciti di Avvio manuale',
    }));
    await waitFor(() => expect(runsRetryFailedMock).toHaveBeenCalledWith('run-1'));
    await waitFor(() => expect(runsListMock).toHaveBeenCalledTimes(2));
    expect(runsGetMock).toHaveBeenCalledWith('run-1');
  });

  it('retry episodio dal run espanso: api.pipeline.retry e refresh', async () => {
    runsGetMock.mockResolvedValue({
      run: run(),
      jobs: [job({ id: 202, fase: 'failed', errore: 'rete', tentativi: 2 })],
    });
    render(AttivitaPage);
    await screen.findByText('Avvio manuale');
    await fireEvent.click(document.querySelector('[data-run-id="run-1"] .run-main'));
    expect(await screen.findByText('Serie X · Ep. 1')).toBeTruthy();
    await fireEvent.click(screen.getByRole('button', { name: 'Dettagli di Serie X · Ep. 1' }));
    await fireEvent.click(screen.getByRole('button', { name: 'Riprova Serie X · Ep. 1' }));
    await waitFor(() => expect(pipelineRetryMock).toHaveBeenCalledWith(202));
    await waitFor(() => expect(runsListMock).toHaveBeenCalledTimes(2));
  });

  it('nessun job_key nel DOM anche espandendo run e dettagli', async () => {
    runsGetMock.mockResolvedValue({
      run: run(),
      jobs: [job({ id: 303, fase: 'failed', errore: 'rete' })],
    });
    render(AttivitaPage);
    await screen.findByText('Avvio manuale');
    await fireEvent.click(document.querySelector('[data-run-id="run-1"] .run-main'));
    await screen.findByText('Serie X · Ep. 1');
    await fireEvent.click(screen.getByRole('button', { name: 'Dettagli di Serie X · Ep. 1' }));
    expect(document.body.textContent).not.toContain('animeW_scraper:/play/x#1');
  });

  it('stati vuoti dedicati per vista e tab', async () => {
    runsListMock.mockResolvedValue({ items: [], total: 0, counts: { in_corso: 0, recenti: 0, problemi: 0 } });
    pipelineListMock.mockResolvedValue({ items: [] });
    render(AttivitaPage);
    expect(await screen.findByText('Nessun avvio recente.')).toBeTruthy();
    await fireEvent.click(screen.getByRole('tab', { name: 'Problemi' }));
    expect(await screen.findByText('Nessun avvio con problemi.')).toBeTruthy();
    await fireEvent.click(screen.getByRole('tab', { name: 'Storico' }));
    expect(await screen.findByText('Nessun avvio nello storico.')).toBeTruthy();
    await fireEvent.click(screen.getByRole('button', { name: 'Episodi' }));
    expect(await screen.findByText('Nessun episodio nello storico.')).toBeTruthy();
    await fireEvent.click(screen.getByRole('tab', { name: 'Problemi' }));
    expect(await screen.findByText('Nessun episodio con problemi.')).toBeTruthy();
    await fireEvent.click(screen.getByRole('tab', { name: 'Recenti' }));
    expect(await screen.findByText('Nessuna attività recente.')).toBeTruthy();
  });

  it('errore di caricamento: alert e Riprova ricarica', async () => {
    runsListMock.mockRejectedValueOnce(new Error('offline'));
    render(AttivitaPage);
    expect(await screen.findByText('offline')).toBeTruthy();
    expect(screen.getByRole('alert')).toBeTruthy();
    await fireEvent.click(screen.getByRole('button', { name: 'Riprova' }));
    expect(await screen.findByText('Avvio manuale')).toBeTruthy();
  });

  it('polling 5s silenzioso: salta se document.hidden e non tocca Aggiorna', async () => {
    vi.useFakeTimers();
    let nascosto = true;
    const descrittore = Object.getOwnPropertyDescriptor(document, 'hidden');
    Object.defineProperty(document, 'hidden', {
      configurable: true, get: () => nascosto,
    });
    try {
      render(AttivitaPage);
      await vi.advanceTimersByTimeAsync(0);
      expect(runsListMock).toHaveBeenCalledTimes(1);
      const aggiorna = screen.getByRole('button', { name: 'Aggiorna attività' });
      expect(aggiorna.textContent.trim()).toBe('Aggiorna');

      await vi.advanceTimersByTimeAsync(5000);
      expect(runsListMock).toHaveBeenCalledTimes(1);

      nascosto = false;
      await vi.advanceTimersByTimeAsync(5000);
      expect(runsListMock).toHaveBeenCalledTimes(2);
      // Il polling è silenzioso: il pulsante resta "Aggiorna".
      expect(screen.getByRole('button', { name: 'Aggiorna attività' }).textContent.trim())
        .toBe('Aggiorna');
    } finally {
      cleanup();
      if (descrittore) Object.defineProperty(document, 'hidden', descrittore);
      else delete document.hidden;
      vi.useRealTimers();
    }
  });
});
