import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen, fireEvent, waitFor } from '@testing-library/svelte';
import NotificationBell from './NotificationBell.svelte';

const { listMock, readMock } = vi.hoisted(() => ({
  listMock: vi.fn(),
  readMock: vi.fn(),
}));

vi.mock('../api.js', () => ({
  api: {
    notifications: { list: listMock, read: readMock },
    // EventSource finto: il componente ne crea uno in onMount e lo chiude
    // allo smontaggio. L'istanza resta accessibile per emettere eventi.
    sse: () => {
      const es = {
        onmessage: null,
        closed: false,
        close() {
          this.closed = true;
        },
        emit(data) {
          this.onmessage?.({ data: JSON.stringify(data) });
        },
      };
      globalThis.__sse = es;
      return es;
    },
  },
}));

beforeEach(() => {
  vi.clearAllMocks();
  delete globalThis.__sse;
  listMock.mockResolvedValue({
    items: [{ id: 7, serie: 'One Piece', episodio: 3, letta: false }],
    unread: 1,
  });
  readMock.mockResolvedValue({ success: true, unread: 0 });
});

describe('NotificationBell', () => {
  it('disabilitata: non monta campanella né connessione SSE', () => {
    render(NotificationBell, { enabled: false });
    expect(screen.queryByLabelText('Notifiche')).toBeNull();
    expect(globalThis.__sse).toBeUndefined();
  });

  it('mostra il badge e apre il pannello con la lista', async () => {
    render(NotificationBell, { enabled: true, sorgente: 'ui' });
    await waitFor(() => expect(screen.getByText('1')).toBeTruthy());
    await fireEvent.click(screen.getByLabelText('Notifiche'));
    expect(screen.getByText('One Piece — Ep. 3')).toBeTruthy();
    expect(globalThis.__sse).toBeTruthy();
  });

  it('click su una notifica la segna letta e azzera il badge', async () => {
    render(NotificationBell, { enabled: true, sorgente: 'ui' });
    await fireEvent.click(screen.getByLabelText('Notifiche'));
    const item = await screen.findByText('One Piece — Ep. 3');
    await fireEvent.click(item);
    await waitFor(() => expect(readMock).toHaveBeenCalledWith(7));
    await waitFor(() => expect(screen.queryByText('1')).toBeNull());
  });

  it('un evento SSE notification aggiorna lista e badge', async () => {
    render(NotificationBell, { enabled: true, sorgente: 'ui' });
    await fireEvent.click(screen.getByLabelText('Notifiche'));
    await screen.findByText('One Piece — Ep. 3');
    globalThis.__sse.emit({ type: 'notification', id: 8, serie: 'Naruto', episodio: 1 });
    expect(await screen.findByText('Naruto — Ep. 1')).toBeTruthy();
    expect(screen.getByText('2')).toBeTruthy();
  });

  it('eventi SSE diversi da notification vengono ignorati', async () => {
    render(NotificationBell, { enabled: true, sorgente: 'ui' });
    await waitFor(() => expect(globalThis.__sse).toBeTruthy());
    globalThis.__sse.emit({ type: 'progress', series: 'X', episode: 1 });
    expect(screen.getByText('1')).toBeTruthy();
    expect(screen.queryByText('X — Ep. 1')).toBeNull();
  });
});
