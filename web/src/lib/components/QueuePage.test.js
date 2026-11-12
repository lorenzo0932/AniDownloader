import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen, fireEvent, waitFor } from '@testing-library/svelte';
import QueuePage from './QueuePage.svelte';

const { listMock, retryMock, schedulerMock } = vi.hoisted(() => ({
  listMock: vi.fn(),
  retryMock: vi.fn(),
  schedulerMock: vi.fn(),
}));

vi.mock('../api.js', () => ({
  api: {
    queue: { list: listMock, retry: retryMock },
    scheduler: { status: schedulerMock },
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
  creato_il: new Date().toISOString(),
  errore: '',
  ...over,
});

beforeEach(() => {
  vi.clearAllMocks();
  schedulerMock.mockResolvedValue({
    attivo: true,
    inPausa: false,
    checkInCoda: 1,
    ultimoGiro: Math.floor(Date.now() / 1000) - 300,
    prossimoGiro: Math.floor(Date.now() / 1000) + 600,
    intervalloMinuti: 15,
    ultimoEsito: 'ok',
    ultimoMessaggio: 'giro completato: 2/2 riusciti',
  });
});

describe('QueuePage', () => {
  it('mostra riepilogo, problemi recenti e coda attiva', async () => {
    listMock.mockResolvedValue({
      items: [
        voce({ id: 1 }),
        voce({
          id: 2, stato: 'fallito', tentativi: 3, serie: 'Naruto',
          errore: 'timeout di rete',
        }),
      ],
      counts: { attesa: 1, attivo: 0, fatto: 1479, fallito: 1 },
    });
    render(QueuePage);
    expect(await screen.findByText('Problemi recenti')).toBeTruthy();
    expect(screen.getByText(/Naruto/)).toBeTruthy();
    expect(screen.getByText('timeout di rete')).toBeTruthy();
    expect(screen.getByText('Ultimo controllo')).toBeTruthy();
    expect(screen.getByText('Prossimo')).toBeTruthy();
    expect(screen.getByText('Attivo')).toBeTruthy();
    expect(screen.getByText('In coda')).toBeTruthy();
    expect(screen.getByText('1479 completati nello storico')).toBeTruthy();
    expect(screen.getAllByText('Riprova ora')).toHaveLength(2);
  });

  it('"Riprova ora" chiama l\'API e ricarica la coda', async () => {
    listMock.mockResolvedValue({
      items: [voce({ id: 5, stato: 'fallito', tentativi: 2, errore: 'rete' })],
      counts: { attesa: 0, attivo: 0, fatto: 0, fallito: 1 },
    });
    retryMock.mockResolvedValue({ success: true, id: 5 });
    render(QueuePage);
    const btn = await screen.findByRole('button', { name: 'Riprova task 5' });
    await fireEvent.click(btn);
    await waitFor(() => expect(retryMock).toHaveBeenCalledWith(5));
    await waitFor(() => expect(listMock).toHaveBeenCalledTimes(2));
  });

  it('i falliti vecchi finiscono nello storico compresso, non nella vista principale', async () => {
    listMock.mockResolvedValue({
      items: [voce({
        id: 9, stato: 'fallito', tentativi: 3,
        creato_il: '2026-11-08T10:00:00Z', errore: 'lock occupato',
      })],
      counts: { attesa: 0, attivo: 0, fatto: 0, fallito: 1 },
    });
    const { container } = render(QueuePage);
    expect(await screen.findByText(/Storico/)).toBeTruthy();
    expect(screen.queryByText('Problemi recenti')).toBeNull();
    await fireEvent.click(container.querySelector('details.storico summary'));
    expect(await screen.findByText('lock occupato')).toBeTruthy();
    expect(screen.queryByText(/Motivo non disponibile/)).toBeNull();
  });

  it('coda vuota: messaggio dedicato e nessun falso problema', async () => {
    listMock.mockResolvedValue({
      items: [],
      counts: { attesa: 0, attivo: 0, fatto: 0, fallito: 0 },
    });
    render(QueuePage);
    expect(await screen.findByText(
      'Nessun controllo in coda. I controlli completati non richiedono azione.'
    )).toBeTruthy();
    expect(screen.queryByText('Problemi recenti')).toBeNull();
  });
});
