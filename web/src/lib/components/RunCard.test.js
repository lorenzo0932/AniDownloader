import { describe, it, expect, vi } from 'vitest';
import { render, screen, fireEvent } from '@testing-library/svelte';

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
      coda: { queued: 2, downloading: 1, converting: 1, retry: 0, totale: 4 },
      problemi: { non_riusciti: 1, interrotti: 0 },
      onopen,
    });

    expect(screen.getByText('Avvio in corso')).toBeTruthy();
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
    expect(screen.getByText('7 totali')).toBeTruthy();
    expect(screen.getByText(/Coda: 2 in attesa/)).toBeTruthy();

    const link = screen.getByRole('link', { name: 'Apri attività' });
    expect(link.getAttribute('href')).toBe('/attivita');
    fireEvent.click(link);
    expect(onopen).toHaveBeenCalledTimes(1);
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
    expect(screen.getByText('0 non riusciti')).toBeTruthy();
    expect(screen.getByText(/Durata 7 min/)).toBeTruthy();
    expect(screen.getByRole('progressbar').getAttribute('aria-valuenow')).toBe('100');
    expect(screen.getByRole('link', { name: 'Vedi dettaglio' }).getAttribute('href')).toBe('/attivita');
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
      problemi: { non_riusciti: 2, interrotti: 0 },
    });

    expect(screen.getByText('Completato con errori')).toBeTruthy();
    expect(screen.getByText('2 non riusciti')).toBeTruthy();
    expect(screen.getByText('Problemi registrati: 2')).toBeTruthy();
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
