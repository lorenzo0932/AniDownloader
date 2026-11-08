import { describe, it, expect, vi } from 'vitest';
import { render, screen, fireEvent } from '@testing-library/svelte';
import RunRow from './RunRow.svelte';

vi.mock('../api.js', () => ({
  posterUrl: (path, w = 480) => '/poster?w=' + w + '&path=' + encodeURIComponent(path),
  posterSrcSet: (path) => '/poster?w=96&path=' + encodeURIComponent(path) + ' 96w',
}));

const run = (over = {}) => ({
  id: 'run-1',
  origine: 'manuale',
  avviato_il: new Date(Date.now() - 5 * 60 * 1000).toISOString(),
  concluso_il: null,
  stato: 'in_corso',
  totale: 7,
  completati: 3,
  non_riusciti: 1,
  saltati: 0,
  in_corso: 2,
  percent: 57,
  ...over,
});

const job = (over = {}) => ({
  id: 11,
  run_id: 'run-1',
  job_key: 'job-key-segreta',
  path: '/media/Psyren',
  serie: 'Psyren',
  episodio: 4,
  origine: 'interactive',
  fase: 'downloading',
  tentativi: 0,
  prossima_esecuzione: 0,
  errore: '',
  aggiornato_il: new Date().toISOString(),
  ...over,
});

describe('RunRow', () => {
  it('header: titolo, origine, totale, conteggi compatti e stato', () => {
    render(RunRow, { run: run(), jobs: [] });
    expect(screen.getByText('Avvio manuale')).toBeTruthy();
    expect(screen.getByText('manuale')).toBeTruthy();
    expect(screen.getByText('7 episodi')).toBeTruthy();
    expect(screen.getByText('✓ 3')).toBeTruthy();
    expect(screen.getByText('◷ 2')).toBeTruthy();
    expect(screen.getByText('! 1')).toBeTruthy();
    expect(screen.getByText('In corso')).toBeTruthy();
  });

  it('mostra i saltati accanto ai conteggi', () => {
    render(RunRow, { run: run({ saltati: 2 }), jobs: [] });
    expect(screen.getByText('– 2')).toBeTruthy();
  });

  it('progressbar determinata con aria-valuenow e aria-valuetext', () => {
    render(RunRow, { run: run(), jobs: [] });
    const barra = screen.getByRole('progressbar');
    expect(barra.getAttribute('aria-valuenow')).toBe('57');
    expect(barra.getAttribute('aria-valuetext')).toBe('3 di 7 episodi');
  });

  it('run concluso: niente progressbar, ma durata formattata', () => {
    render(RunRow, {
      run: run({
        avviato_il: '2026-11-08T17:03:00.000Z',
        concluso_il: '2026-11-08T17:08:12.000Z',
        stato: 'completato',
        percent: 100, non_riusciti: 0, in_corso: 0, completati: 7,
      }),
      jobs: [],
    });
    expect(screen.queryByRole('progressbar')).toBeNull();
    expect(screen.getByText('5m 12s')).toBeTruthy();
    expect(screen.getByText('Completato')).toBeTruthy();
  });

  it('"Riprova tutti" è separato dal bottone di espansione', async () => {
    const onRetryFailed = vi.fn();
    const onToggle = vi.fn();
    const { container } = render(RunRow, {
      run: run(), jobs: [], expanded: true, onRetryFailed, onToggle,
    });
    const retry = screen.getByRole('button', {
      name: 'Riprova tutti i non riusciti di Avvio manuale',
    });
    expect(retry.closest('.run-main')).toBeNull();
    await fireEvent.click(retry);
    expect(onRetryFailed).toHaveBeenCalledWith(expect.objectContaining({ id: 'run-1' }));

    const main = container.querySelector('.run-main');
    await fireEvent.click(main);
    expect(onToggle).toHaveBeenCalledWith(expect.objectContaining({ id: 'run-1' }));
  });

  it('senza non riusciti non c\'è "Riprova tutti"', () => {
    render(RunRow, { run: run({ non_riusciti: 0 }), jobs: [], expanded: true });
    expect(screen.queryByRole('button', { name: /Riprova tutti/ })).toBeNull();
  });

  it('espansione: elenca i JobRow e passa i retry del singolo episodio', async () => {
    const onRetryJob = vi.fn();
    render(RunRow, {
      run: run(), expanded: true,
      jobs: [job({ id: 12, serie: 'Psyren', episodio: 3, fase: 'failed', errore: 'rete' })],
      onRetryJob,
    });
    expect(screen.getByText('Psyren · Ep. 3')).toBeTruthy();
    await fireEvent.click(screen.getByRole('button', { name: 'Dettagli di Psyren · Ep. 3' }));
    await fireEvent.click(screen.getByRole('button', { name: 'Riprova Psyren · Ep. 3' }));
    expect(onRetryJob).toHaveBeenCalledWith(expect.objectContaining({ id: 12 }));
  });

  it('espansione con loading: skeleton, nessun episodio', () => {
    const { container } = render(RunRow, {
      run: run(), expanded: true, loading: true, jobs: [job()],
    });
    expect(container.querySelector('.skeleton')).not.toBeNull();
    expect(screen.queryByText('Psyren · Ep. 4')).toBeNull();
  });

  it('espansione con errore: messaggio e Riprova che ricarica', async () => {
    const onReload = vi.fn();
    render(RunRow, {
      run: run(), expanded: true, error: 'rete assente', jobs: [], onReload,
    });
    expect(screen.getByText('rete assente')).toBeTruthy();
    await fireEvent.click(screen.getByRole('button', { name: 'Riprova' }));
    expect(onReload).toHaveBeenCalledWith(expect.objectContaining({ id: 'run-1' }));
  });

  it('run vuoto espanso: messaggio dedicato', () => {
    render(RunRow, { run: run(), expanded: true, jobs: [] });
    expect(screen.getByText('Nessun episodio in questo avvio.')).toBeTruthy();
  });

  it('niente job_key nel DOM', () => {
    const { container } = render(RunRow, {
      run: run(), expanded: true, jobs: [job({ expanded: true })],
    });
    expect(container.textContent).not.toContain('job-key-segreta');
  });
});
