import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen, fireEvent, waitFor } from '@testing-library/svelte';
import NotificationBell from './NotificationBell.svelte';

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

  it('mostra il badge e apre il pannello raggruppato per serie', async () => {
    const { container } = render(NotificationBell, { enabled: true, sorgente: 'ui' });
    await waitFor(() => expect(container.querySelector('.notif-badge')?.textContent).toBe('1'));
    await fireEvent.click(screen.getByLabelText('Notifiche'));
    expect(screen.getByText('One Piece')).toBeTruthy();
    expect(screen.getByText('Ep. 3')).toBeTruthy();
    expect(globalThis.__sse).toBeTruthy();
  });

  it('click su una notifica la segna letta e apre la serie in Gestione', async () => {
    const aperture = [];
    const onOpen = (e) => aperture.push(e.detail);
    window.addEventListener('anidl:open-series', onOpen);
    const { container } = render(NotificationBell, { enabled: true, sorgente: 'ui' });
    await fireEvent.click(screen.getByLabelText('Notifiche'));
    const item = await screen.findByRole('button', {
      name: 'Apri One Piece, episodio 3',
    });
    await fireEvent.click(item);
    await waitFor(() => expect(readMock).toHaveBeenCalledWith(7));
    await waitFor(() => expect(aperture).toEqual([{ serie: 'One Piece' }]));
    // La notifica resta nello storico, ma non è più non letta.
    expect(container.querySelector('.notif-badge')).toBeNull();
    await fireEvent.click(screen.getByLabelText('Notifiche'));
    expect(screen.getByText('One Piece')).toBeTruthy();
    window.removeEventListener('anidl:open-series', onOpen);
  });

  it('un evento SSE notification aggiorna lista e badge', async () => {
    const { container } = render(NotificationBell, { enabled: true, sorgente: 'ui' });
    await fireEvent.click(screen.getByLabelText('Notifiche'));
    await screen.findByText('One Piece');
    globalThis.__sse.emit({ type: 'notification', id: 8, serie: 'Naruto', episodio: 1 });
    expect(await screen.findByText('Naruto')).toBeTruthy();
    expect(await screen.findByText('Ep. 1')).toBeTruthy();
    expect(container.querySelector('.notif-badge')?.textContent).toBe('2');
  });

  it('abilitata a runtime: si connette senza rimontare', async () => {
    const { rerender, container } = render(NotificationBell, { enabled: false, sorgente: 'ui' });
    expect(globalThis.__sse).toBeUndefined();
    await rerender({ enabled: true, sorgente: 'ui' });
    await waitFor(() => expect(globalThis.__sse).toBeTruthy());
    await waitFor(() => expect(container.querySelector('.notif-badge')?.textContent).toBe('1'));
  });

  it('eventi SSE diversi da notification vengono ignorati', async () => {
    const { container } = render(NotificationBell, { enabled: true, sorgente: 'ui' });
    await waitFor(() => expect(globalThis.__sse).toBeTruthy());
    globalThis.__sse.emit({ type: 'progress', series: 'X', episode: 1 });
    expect(container.querySelector('.notif-badge')?.textContent).toBe('1');
    expect(screen.queryByText('X')).toBeNull();
  });

  it('se il demone ha già inviato la notifica OS, il client non la duplica', async () => {
    const invoke = vi.fn().mockResolvedValue();
    window.__TAURI_INTERNALS__ = { invoke };
    try {
      const { container } = render(NotificationBell, {
        enabled: true, sorgente: 'entrambi', osNotifyBackend: true,
      });
      await waitFor(() => expect(globalThis.__sse).toBeTruthy());
      globalThis.__sse.emit({
        type: 'notification', id: 9, serie: 'Bleach', episodio: 2, os: true,
      });
      await waitFor(() =>
        expect(container.querySelector('.notif-badge')?.textContent).toBe('2'));
      expect(invoke).not.toHaveBeenCalled();
    } finally {
      delete window.__TAURI_INTERNALS__;
    }
  });

  it('se il demone fallisce la notifica OS, il client Tauri fa fallback', async () => {
    const invoke = vi.fn().mockResolvedValue();
    window.__TAURI_INTERNALS__ = { invoke };
    try {
      const { container } = render(NotificationBell, {
        enabled: true, sorgente: 'entrambi', osNotifyBackend: true,
      });
      await waitFor(() => expect(globalThis.__sse).toBeTruthy());
      globalThis.__sse.emit({
        type: 'notification', id: 10, serie: 'Bleach', episodio: 3, os: false,
      });
      await waitFor(() =>
        expect(container.querySelector('.notif-badge')?.textContent).toBe('2'));
      await waitFor(() => expect(invoke).toHaveBeenCalledWith('notify_episode', {
        title: 'Nuovo episodio pronto',
        body: 'Bleach — Ep. 3',
      }));
    } finally {
      delete window.__TAURI_INTERNALS__;
    }
  });
});