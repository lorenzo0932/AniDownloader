import { describe, it, expect, vi } from 'vitest';
import { render, screen, fireEvent, cleanup } from '@testing-library/svelte';

import RunCard from './RunCard.svelte';

const baseRun = (over = {}) => ({
  id: 'run-1794153364250-0',
  origine: 'manuale',
  avviato_il: '2026-11-08T17:03:00.000Z',
  concluso_il: null,
  stato: 'in_corso',
  totale: 7,
  completati: 3,
  non_riusciti: 1,
  saltati: 0,
  in_corso: 3,
  percent: 57,
  corrente: { serie: 'Psyren', episodio: 4, fase: 'converting' },
  ...over,
});

describe('RunCard', () => {
  it('nessun run: empty state con invito al prossimo controllo', () => {
    render(RunCard, { run: null });
    expect(screen.getByText(/Nessun avvio registrato/)).toBeTruthy();
    expect(screen.getByText(/prossimo controllo automatico/)).toBeTruthy();
    expect(screen.queryByRole('progressbar')).toBeNull();
  });

  it('run in corso: titolo, barra determinata, corrente, conteggi e CTA', () => {
    const onopen = vi.fn();
    render(RunCard, {
      run: baseRun(),
      onopen,
    });

    expect(screen.getByText('Avvio corrente')).toBeTruthy();
    expect(screen.getByText('manuale')).toBeTruthy();

    const bar = screen.getByRole('progressbar');
    expect(bar.getAttribute('aria-valuenow')).toBe('57');
    expect(bar.getAttribute('aria-valuemin')).toBe('0');
    expect(bar.getAttribute('aria-valuemax')).toBe('100');
    expect(bar.getAttribute('aria-valuetext')).toBe('3 di 7 episodi');

    expect(screen.getByText('Psyren · Ep. 4 · In conversione')).toBeTruthy();
    expect(screen.getByText('3 completati')).toBeTruthy();
    expect(screen.getByText('3 in corso')).toBeTruthy();
    expect(screen.getByText('1 non riuscito')).toBeTruthy();
    expect(screen.getByText(/7 episodi/)).toBeTruthy();
    // La card mostra solo il run: niente numeri globali di coda/problemi.
    expect(screen.queryByText(/Coda:/)).toBeNull();
    expect(screen.queryByText(/Problemi aperti:/)).toBeNull();

    const link = screen.getByRole('link', { name: 'Apri attività' });
    expect(link.getAttribute('href')).toBe('/attivita');
    fireEvent.click(link);
    expect(onopen).toHaveBeenCalledTimes(1);
  });

  it('task live: barra complessiva con percentuali reali e una riga per episodio', () => {
    render(RunCard, {
      run: baseRun({
        totale: 2, completati: 0, in_corso: 2, percent: 0,
        corrente: { serie: 'Witch', episodio: 6, fase: 'converting' },
      }),
      tasks: [
        { key: 'Witch#6', seriesName: 'Witch', episode: 6, phase: 'converting', percent: 70 },
        { key: 'Psyren#5', seriesName: 'Psyren', episode: 5, phase: 'converting', percent: 36 },
      ],
    });

    const bars = screen.getAllByRole('progressbar');
    expect(bars).toHaveLength(3); // complessiva + 2 episodi
    expect(bars[0].getAttribute('aria-valuenow')).toBe('53');
    expect(bars[1].getAttribute('aria-valuenow')).toBe('70');
    expect(bars[2].getAttribute('aria-valuenow')).toBe('36');
    expect(screen.getByText('Witch')).toBeTruthy();
    expect(screen.getByText('Psyren')).toBeTruthy();
    expect(screen.getByText('70%')).toBeTruthy();
    expect(screen.getByText('36%')).toBeTruthy();
    // Con i task live non serve la riga "corrente" sintetica.
    expect(screen.queryByText('Witch · Ep. 6 · In conversione')).toBeNull();
  });

  it('task senza percent (in attesa): nessuna barra per-task, resta il fallback', () => {
    render(RunCard, {
      run: baseRun({
        totale: 2, completati: 1, in_corso: 1, percent: 50,
        corrente: { serie: 'Psyren', episodio: 5, fase: 'waiting_conversion' },
      }),
      tasks: [
        { key: 'Psyren#5', seriesName: 'Psyren', episode: 5, phase: 'waiting_conversion', percent: -1 },
      ],
    });

    expect(screen.getAllByRole('progressbar')).toHaveLength(1);
    expect(screen.getByText('Psyren')).toBeTruthy();
    expect(screen.getByText('In attesa di conversione')).toBeTruthy();
    expect(screen.queryByText('50%')).toBeNull();
  });

  it('run completato: ultimo avvio, conteggi, durata e CTA dettaglio', () => {
    render(RunCard, {
      run: baseRun({
        stato: 'completato',
        concluso_il: '2026-11-08T17:10:00.000Z',
        completati: 7,
        non_riusciti: 0,
        in_corso: 0,
        percent: 100,
        corrente: null,
      }),
    });

    expect(screen.getByText('Ultimo avvio')).toBeTruthy();
    expect(screen.getByText('Completato')).toBeTruthy();
    expect(screen.getByText('7 completati')).toBeTruthy();
    expect(screen.queryByText('0 non riusciti')).toBeNull();
    expect(screen.getByText(/7 min/)).toBeTruthy();
    expect(screen.queryByRole('progressbar')).toBeNull();
    expect(screen.getByRole('link', { name: 'Vedi dettaglio' }).getAttribute('href')).toBe('/attivita');
  });

  it('run senza episodi: niente barra, messaggio dedicato e saltati', () => {
    render(RunCard, {
      run: baseRun({
        stato: 'completato',
        concluso_il: '2026-11-08T17:10:00.000Z',
        totale: 0, completati: 0, in_corso: 0, non_riusciti: 0,
        saltati: 27, percent: 0, corrente: null,
      }),
    });

    expect(screen.queryByRole('progressbar')).toBeNull();
    expect(screen.getByText('Nessun episodio nuovo')).toBeTruthy();
    expect(screen.getByText('27 serie già aggiornate')).toBeTruthy();
  });

  it('run con errori: conteggio non riusciti in evidenza', () => {
    render(RunCard, {
      run: baseRun({
        stato: 'completato_con_errori',
        concluso_il: '2026-11-08T17:10:00.000Z',
        completati: 5,
        non_riusciti: 2,
        in_corso: 0,
        percent: 100,
        corrente: null,
      }),
    });

    expect(screen.getByText('Completato con errori')).toBeTruthy();
    expect(screen.getByText('2 non riusciti')).toBeTruthy();
    expect(screen.queryByText(/Problemi aperti:/)).toBeNull();
  });

  it('fasi reali del backend: downloaded_waiting_conversion e retry_wait', () => {
    render(RunCard, {
      run: baseRun({ corrente: { serie: 'Psyren', episodio: 4, fase: 'downloaded_waiting_conversion' } }),
    });
    expect(screen.getByText('Psyren · Ep. 4 · In attesa di conversione')).toBeTruthy();
    cleanup();

    render(RunCard, {
      run: baseRun({ corrente: { serie: 'Psyren', episodio: 5, fase: 'retry_wait' } }),
    });
    expect(screen.getByText('Psyren · Ep. 5 · In attesa')).toBeTruthy();
    expect(screen.queryByText(/retry_wait/)).toBeNull();
  });

  it('origine automatica: badge dedicato', () => {
    render(RunCard, {
      run: baseRun({
        origine: 'automatico',
        stato: 'completato',
        concluso_il: '2026-11-08T17:10:00.000Z',
        completati: 7,
        non_riusciti: 0,
        in_corso: 0,
        percent: 100,
        corrente: null,
      }),
    });

    expect(screen.getByText('automatico')).toBeTruthy();
    expect(screen.queryByText('manuale')).toBeNull();
  });
});
