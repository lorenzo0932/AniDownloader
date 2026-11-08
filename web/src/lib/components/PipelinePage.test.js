import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen } from '@testing-library/svelte';
import PipelinePage from './PipelinePage.svelte';

const { listMock } = vi.hoisted(() => ({ listMock: vi.fn() }));

vi.mock('../api.js', () => ({
  api: { pipeline: { list: listMock } },
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
  ...over,
});

beforeEach(() => vi.clearAllMocks());

describe('PipelinePage', () => {
  it('mostra conteggi e job con origine e fase', async () => {
    listMock.mockResolvedValue({
      counts: { queued_download: 1, converting: 1, completed: 3 },
      items: [job(), job({ id: 2, fase: 'converting', serie: 'Serie Y', origine: 'background' })],
    });
    render(PipelinePage);
    expect(await screen.findByText('Serie X')).toBeTruthy();
    expect(screen.getByText('Serie Y')).toBeTruthy();
    expect(screen.getByText('interattivo')).toBeTruthy();
    expect(screen.getAllByText('Conversione').length).toBeGreaterThanOrEqual(2);
    // conteggio completed = 3
    expect(screen.getByText('3')).toBeTruthy();
  });

  it('coda vuota: messaggio dedicato', async () => {
    listMock.mockResolvedValue({ counts: {}, items: [] });
    render(PipelinePage);
    expect(await screen.findByText('Nessun job in coda.')).toBeTruthy();
  });
});
