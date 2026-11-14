import { describe, it, expect, vi } from 'vitest';
import { render, screen, fireEvent } from '@testing-library/svelte';
import JobRow from './JobRow.svelte';

vi.mock('../api.js', () => ({
  posterUrl: (path, w = 480) =>
    path ? '/poster?w=' + w + '&path=' + encodeURIComponent(path) : '',
  posterSrcSet: (path) =>
    path ? '/poster?w=96&path=' + encodeURIComponent(path) + ' 96w' : '',
}));

const job = (over = {}) => ({
  id: 1,
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

describe('JobRow', () => {
  it('mostra titolo, chip "In coda" e nessuno stepper per un job in coda', () => {
    const { container } = render(JobRow, { job: job() });
    expect(screen.getByText('Serie X · Ep. 1')).toBeTruthy();
    expect(screen.getByText('In coda')).toBeTruthy();
    expect(container.querySelector('.stepper')).toBeNull();
  });

  it('locandina 40x56 lazy e async', () => {
    const { container } = render(JobRow, { job: job() });
    const img = container.querySelector('img.job-poster');
    expect(img).not.toBeNull();
    expect(img.getAttribute('loading')).toBe('lazy');
    expect(img.getAttribute('decoding')).toBe('async');
    expect(img.getAttribute('width')).toBe('40');
    expect(img.getAttribute('height')).toBe('56');
    expect(img.getAttribute('src')).toContain('/poster?w=96');
  });

  it('senza path usa un avatar con l’iniziale, non un rettangolo vuoto', () => {
    const { container } = render(JobRow, {
      job: job({ path: '', serie: 'Naruto' }),
    });
    expect(container.querySelector('img.job-poster')).toBeNull();
    expect(container.querySelector('.job-poster-empty').textContent.trim()).toBe('N');
  });

  it('prop poster sovrascrive la locandina calcolata', () => {
    const { container } = render(JobRow, { job: job(), poster: '/custom.webp' });
    expect(container.querySelector('img.job-poster').getAttribute('src')).toBe('/custom.webp');
  });

  it('job attivo: stepper compatto sotto la riga', () => {
    const { container } = render(JobRow, { job: job({ fase: 'converting' }) });
    expect(screen.getByText('In conversione')).toBeTruthy();
    const stepper = container.querySelector('.job-stepper .stepper');
    expect(stepper).not.toBeNull();
    expect(container.querySelector('[data-step="Conversione"]').getAttribute('data-stato'))
      .toBe('current');
  });

  it('retry_wait: chip con orario di prossima esecuzione', () => {
    const sec = Math.floor(Date.now() / 1000) + 300;
    render(JobRow, { job: job({ fase: 'retry_wait', prossima_esecuzione: sec }) });
    const ora = new Date(sec * 1000).toLocaleTimeString('it-IT', {
      hour: '2-digit', minute: '2-digit',
    });
    expect(screen.getByText(`In attesa · riprovo alle ${ora}`)).toBeTruthy();
  });

  it('espansione: aria-expanded, dettagli e niente job_key', async () => {
    const onToggle = vi.fn();
    const { container } = render(JobRow, { job: job({ tentativi: 2 }), onToggle });
    const riga = screen.getByRole('button', { name: 'Dettagli di Serie X · Ep. 1' });
    expect(riga.getAttribute('aria-expanded')).toBe('false');
    await fireEvent.click(riga);
    expect(onToggle).toHaveBeenCalledWith(expect.objectContaining({ id: 1 }));

    const { container: aperto } = render(JobRow, {
      job: job({ tentativi: 2, errore: 'rete' }), expanded: true,
    });
    expect(aperto.textContent).toContain('Tentativi');
    expect(aperto.textContent).toContain('manuale');
    expect(aperto.textContent).toContain('rete');
    expect(aperto.textContent).not.toContain('animeW_scraper:/play/x#1');
    expect(container.textContent).not.toContain('animeW_scraper:/play/x#1');
    expect(screen.queryByRole('button', { name: /^Riprova / })).toBeNull();
  });

  it('job fallito espanso: "Non riuscito", errore e Riprova', async () => {
    const onRetry = vi.fn();
    render(JobRow, {
      job: job({ id: 2, serie: 'Serie Y', episodio: 2, fase: 'failed', errore: 'rete' }),
      expanded: true,
      onRetry,
    });
    expect(screen.getByText('Non riuscito')).toBeTruthy();
    expect(screen.getByText('rete')).toBeTruthy();
    const btn = screen.getByRole('button', { name: 'Riprova Serie Y · Ep. 2' });
    await fireEvent.click(btn);
    expect(onRetry).toHaveBeenCalledWith(expect.objectContaining({ id: 2 }));
  });

  it('completato espanso: nessun pulsante Riprova', () => {
    render(JobRow, {
      job: job({ fase: 'completed', serie: 'Serie Z', episodio: 3 }), expanded: true,
    });
    expect(screen.getByText('Completato')).toBeTruthy();
    expect(screen.queryByRole('button', { name: /Riprova/ })).toBeNull();
  });
});
