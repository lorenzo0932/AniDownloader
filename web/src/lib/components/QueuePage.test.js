import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen, fireEvent, waitFor } from '@testing-library/svelte';
import QueuePage from './QueuePage.svelte';

const { listMock, retryMock } = vi.hoisted(() => ({
  listMock: vi.fn(),
  retryMock: vi.fn(),
}));

vi.mock('../api.js', () => ({
  api: {
    queue: { list: listMock, retry: retryMock },
    sse: () => ({ onmessage: null, close() {} }),
  },
}));

const voce = (over = {}) => ({
  id: 1,
  kind: 'check',
  serie: 'One Piece',
  episodio: 0,
  stato: 'attesa',
  tentativi: 0,
  prossima_esecuzione: 0,
  priorita: 0,
  creato_il: '2026-11-08T10:00:00Z',
  ...over,
});

beforeEach(() => {
  vi.clearAllMocks();
});

describe('QueuePage', () => {
  it('mostra conteggi e righe della coda', async () => {
    listMock.mockResolvedValue({
      items: [
        voce({ id: 1 }),
        voce({ id: 2, stato: 'fallito', tentativi: 3, serie: 'Naruto' }),
      ],
      counts: { attesa: 1, attivo: 0, fatto: 0, fallito: 1 },
    });
    render(QueuePage);
    expect(await screen.findByText('One Piece')).toBeTruthy();
    expect(screen.getByText('Naruto')).toBeTruthy();
    expect(screen.getByText('Fallito')).toBeTruthy();
    expect(screen.getAllByText('Riprova ora')).toHaveLength(2);
  });

  it('"Riprova ora" chiama l\'API e ricarica la coda', async () => {
    listMock.mockResolvedValue({
      items: [voce({ id: 5, stato: 'fallito', tentativi: 2 })],
      counts: { attesa: 0, attivo: 0, fatto: 0, fallito: 1 },
    });
    retryMock.mockResolvedValue({ success: true, id: 5 });
    render(QueuePage);
    const btn = await screen.findByRole('button', { name: 'Riprova task 5' });
    await fireEvent.click(btn);
    await waitFor(() => expect(retryMock).toHaveBeenCalledWith(5));
    await waitFor(() => expect(listMock).toHaveBeenCalledTimes(2));
  });

  it('coda vuota: messaggio dedicato', async () => {
    listMock.mockResolvedValue({
      items: [],
      counts: { attesa: 0, attivo: 0, fatto: 0, fallito: 0 },
    });
    render(QueuePage);
    expect(await screen.findByText('Coda vuota.')).toBeTruthy();
  });
});
