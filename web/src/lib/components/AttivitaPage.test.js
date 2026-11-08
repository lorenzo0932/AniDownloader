import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen, fireEvent, waitFor } from '@testing-library/svelte';
import AttivitaPage from './AttivitaPage.svelte';

const { listMock, retryMock } = vi.hoisted(() => ({
  listMock: vi.fn(),
  retryMock: vi.fn(),
}));

vi.mock('../api.js', () => ({
  api: { pipeline: { list: listMock, retry: retryMock } },
}));

const job = (over = {}) => ({
  id: 1,
  job_key: 'animeW_scraper:/play/x#1',
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
  ],
});

beforeEach(() => {
  vi.clearAllMocks();
  listMock.mockResolvedValue(lista());
  retryMock.mockResolvedValue({ success: true, id: 2 });
});

describe('AttivitaPage', () => {
  it('tab In corso: righe leggibili con serie, episodio e stato', async () => {
    render(AttivitaPage);
    expect(await screen.findByText('Serie X · Ep. 1')).toBeTruthy();
    expect(screen.getByText('Serie Y · Ep. 2')).toBeTruthy();
    expect(screen.getByText('In download')).toBeTruthy();
    expect(screen.getByText('Errore')).toBeTruthy();
    // I completati non stanno nella tab In corso.
    expect(screen.queryByText('Serie Z · Ep. 3')).toBeNull();
  });

  it('tab Storico: solo completati', async () => {
    render(AttivitaPage);
    await screen.findByText('Serie X · Ep. 1');
    await fireEvent.click(screen.getByRole('tab', { name: 'Storico' }));
    expect(await screen.findByText('Serie Z · Ep. 3')).toBeTruthy();
    expect(screen.getByText('Completato')).toBeTruthy();
    expect(screen.queryByText('Serie X · Ep. 1')).toBeNull();
  });

  it('filtro Errori: resta solo il job fallito', async () => {
    render(AttivitaPage);
    await screen.findByText('Serie X · Ep. 1');
    await fireEvent.click(screen.getByRole('button', { name: 'Errori' }));
    expect(await screen.findByText('Serie Y · Ep. 2')).toBeTruthy();
    expect(screen.queryByText('Serie X · Ep. 1')).toBeNull();
    expect(screen.getByRole('button', { name: 'Errori' }).getAttribute('aria-pressed')).toBe('true');
  });

  it('dettagli espandibili mostrano JobKey, origine e tentativi', async () => {
    render(AttivitaPage);
    await screen.findByText('Serie Y · Ep. 2');
    const riga = screen.getByRole('button', { name: 'Dettagli di Serie Y · Ep. 2' });
    expect(riga.getAttribute('aria-expanded')).toBe('false');
    await fireEvent.click(riga);
    expect(riga.getAttribute('aria-expanded')).toBe('true');
    expect(await screen.findByText('animeW_scraper:/play/x#1')).toBeTruthy();
    expect(screen.getByText('interattivo')).toBeTruthy();
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
    expect(await screen.findByText('Nessuna attività in corso.')).toBeTruthy();
    await fireEvent.click(screen.getByRole('tab', { name: 'Storico' }));
    expect(await screen.findByText('Nessun episodio completato.')).toBeTruthy();
  });
});
