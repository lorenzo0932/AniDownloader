import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen, fireEvent, waitFor } from '@testing-library/svelte';
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

const { listMock, retryMock } = vi.hoisted(() => ({
  listMock: vi.fn(),
  retryMock: vi.fn(),
}));

vi.mock('../api.js', () => ({
  api: { pipeline: { list: listMock, retry: retryMock } },
  posterUrl: (path, w = 480) => '/poster?w=' + w + '&path=' + encodeURIComponent(path),
  posterSrcSet: (path) => '/poster?w=96&path=' + encodeURIComponent(path) + ' 96w',
}));

const job = (over = {}) => ({
  id: 1,
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

const lista = () => ({
  counts: {},
  items: [
    job({ id: 1, serie: 'Serie X', fase: 'downloading' }),
    job({ id: 2, serie: 'Serie Y', episodio: 2, fase: 'failed', tentativi: 2, errore: 'rete' }),
    job({ id: 3, serie: 'Serie Z', episodio: 3, fase: 'completed' }),
    job({ id: 4, serie: 'Serie W', episodio: 4, fase: 'completed',
          aggiornato_il: new Date(Date.now() - 3 * 24 * 60 * 60 * 1000).toISOString() }),
  ],
});

beforeEach(() => {
  vi.clearAllMocks();
  listMock.mockResolvedValue(lista());
  retryMock.mockResolvedValue({ success: true, id: 2 });
});

describe('AttivitaPage', () => {
  it('tab Recenti: attivi e conclusi nelle ultime 24h, non lo storico vecchio', async () => {
    render(AttivitaPage);
    expect(await screen.findByText('Serie X · Ep. 1')).toBeTruthy();
    expect(screen.getByText('Serie Y · Ep. 2')).toBeTruthy();
    expect(screen.getByText('In download')).toBeTruthy();
    expect(screen.getByText('Errore')).toBeTruthy();
    // Il completato recente resta nei Recenti; quello di 3 giorni fa no.
    expect(screen.getByText('Serie Z · Ep. 3')).toBeTruthy();
    expect(screen.queryByText('Serie W · Ep. 4')).toBeNull();
  });

  it('tab Storico: solo conclusi da più di 24h', async () => {
    render(AttivitaPage);
    await screen.findByText('Serie X · Ep. 1');
    await fireEvent.click(screen.getByRole('tab', { name: 'Storico' }));
    expect(await screen.findByText('Serie W · Ep. 4')).toBeTruthy();
    expect(screen.getByText('Completato')).toBeTruthy();
    expect(screen.queryByText('Serie X · Ep. 1')).toBeNull();
    expect(screen.queryByText('Serie Z · Ep. 3')).toBeNull();
  });

  it('filtri solo nei Recenti: nello Storico non ci sono, tornando si riparte da Tutti', async () => {
    render(AttivitaPage);
    await screen.findByText('Serie X · Ep. 1');
    expect(screen.getByRole('button', { name: 'Tutti' })).toBeTruthy();
    await fireEvent.click(screen.getByRole('button', { name: 'Errori' }));
    await fireEvent.click(screen.getByRole('tab', { name: 'Storico' }));
    expect(screen.queryByRole('button', { name: 'Tutti' })).toBeNull();
    expect(screen.queryByRole('button', { name: 'Errori' })).toBeNull();
    await fireEvent.click(screen.getByRole('tab', { name: 'Recenti' }));
    const tutti = screen.getByRole('button', { name: 'Tutti' });
    expect(tutti.getAttribute('aria-pressed')).toBe('true');
  });

  it('filtro Errori: resta solo il job fallito', async () => {
    render(AttivitaPage);
    await screen.findByText('Serie X · Ep. 1');
    await fireEvent.click(screen.getByRole('button', { name: 'Errori' }));
    expect(await screen.findByText('Serie Y · Ep. 2')).toBeTruthy();
    expect(screen.queryByText('Serie X · Ep. 1')).toBeNull();
    expect(screen.getByRole('button', { name: 'Errori' }).getAttribute('aria-pressed')).toBe('true');
  });

  it('mostra la locandina della serie nella riga', async () => {
    render(AttivitaPage);
    await screen.findByText('Serie Y · Ep. 2');
    const img = document.querySelector('img.job-poster');
    expect(img).toBeTruthy();
    expect(img.getAttribute('src')).toContain('/poster?w=96');
  });

  it('dettagli espandibili mostrano origine leggibile e tentativi (niente JobKey)', async () => {
    render(AttivitaPage);
    await screen.findByText('Serie Y · Ep. 2');
    const riga = screen.getByRole('button', { name: 'Dettagli di Serie Y · Ep. 2' });
    expect(riga.getAttribute('aria-expanded')).toBe('false');
    await fireEvent.click(riga);
    expect(riga.getAttribute('aria-expanded')).toBe('true');
    expect(screen.queryByText('animeW_scraper:/play/x#1')).toBeNull();
    expect(screen.getByText('manuale')).toBeTruthy();
    expect(screen.getByText('2')).toBeTruthy();
    expect(screen.getByText('rete')).toBeTruthy();
  });

  it('"Riprova" chiama api.pipeline.retry e ricarica la lista', async () => {
    render(AttivitaPage);
    await screen.findByText('Serie Y · Ep. 2');
    await fireEvent.click(screen.getByRole('button', { name: 'Dettagli di Serie Y · Ep. 2' }));
    const btn = await screen.findByRole('button', { name: 'Riprova Serie Y · Ep. 2' });
    await fireEvent.click(btn);
    await waitFor(() => expect(retryMock).toHaveBeenCalledWith(2));
    await waitFor(() => expect(listMock).toHaveBeenCalledTimes(2));
  });

  it('nessun job: messaggio dedicato per tab', async () => {
    listMock.mockResolvedValue({ counts: {}, items: [] });
    render(AttivitaPage);
    expect(await screen.findByText('Nessuna attività recente.')).toBeTruthy();
    await fireEvent.click(screen.getByRole('tab', { name: 'Storico' }));
    expect(await screen.findByText('Nessun episodio nello storico.')).toBeTruthy();
  });
});