import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen, fireEvent, waitFor, cleanup } from '@testing-library/svelte';
import LogsPage from './LogsPage.svelte';

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
    anim.finished = Promise.resolve().then(() => {
      anim.onfinish?.();
    });
    return anim;
  };
}

const { logsMock, queueListMock, queueRetryMock, schedulerMock } = vi.hoisted(() => ({
  logsMock: vi.fn(),
  queueListMock: vi.fn(),
  queueRetryMock: vi.fn(),
  schedulerMock: vi.fn(),
}));

vi.mock('../api.js', () => ({
  api: {
    logs: logsMock,
    queue: { list: queueListMock, retry: queueRetryMock },
    scheduler: { status: schedulerMock },
    sse: () => ({ onmessage: null, close() {} }),
  },
}));

function riga(time, level, message) {
  return `[${time}] [${level.padEnd(6)}] ${message}`;
}

const SAMPLE = [
  riga('2026-11-08 18:33:05', 'INFO', 'Download execution completed'),
  riga('2026-11-08 18:33:06', 'WARN', 'HTML statico non scaricabile'),
  riga('2026-11-08 18:33:07', 'ERROR', 'Download fallito per Psyren Ep. 5'),
  riga('2026-11-08 18:33:08', 'RESULT', 'Conversione completata'),
  'riga senza formato',
];

beforeEach(() => {
  vi.clearAllMocks();
  logsMock.mockResolvedValue({ lines: SAMPLE });
  queueListMock.mockResolvedValue({
    items: [],
    counts: { attesa: 0, attivo: 0, fatto: 0, fallito: 0 },
  });
  queueRetryMock.mockResolvedValue({ success: true });
  schedulerMock.mockResolvedValue({
    attivo: true, inPausa: false, checkInCoda: 0,
    ultimoGiro: 0, prossimoGiro: 0, intervalloMinuti: 15,
    ultimoEsito: '', ultimoMessaggio: '',
  });
});

describe('LogsPage', () => {
  it('carica e mostra le righe nel viewer', async () => {
    render(LogsPage);
    expect(await screen.findByText('Download execution completed')).toBeTruthy();
    expect(logsMock).toHaveBeenCalledWith(500);
    expect(screen.getByRole('log', { name: 'Log del server' })).toBeTruthy();
    expect(screen.getByText('riga senza formato')).toBeTruthy();
  });

  it('chip livelli con conteggi e filtro Errori', async () => {
    render(LogsPage);
    await screen.findByText('Download execution completed');

    expect(screen.getByRole('button', { name: /Tutti/ }).textContent).toContain('5');
    expect(screen.getByRole('button', { name: /Errori/ }).textContent).toContain('1');
    expect(screen.getByRole('button', { name: /Avvisi/ }).textContent).toContain('1');
    expect(screen.getByRole('button', { name: /Info/ }).textContent).toContain('1');
    expect(screen.getByRole('button', { name: /Risultati/ }).textContent).toContain('1');

    const errori = screen.getByRole('button', { name: /Errori/ });
    await fireEvent.click(errori);
    expect(errori.getAttribute('aria-pressed')).toBe('true');
    expect(screen.getByText('Download fallito per Psyren Ep. 5')).toBeTruthy();
    expect(screen.queryByText('Download execution completed')).toBeNull();
    expect(screen.queryByText('riga senza formato')).toBeNull();
  });

  it('chip Tutti ripristina tutte le righe', async () => {
    render(LogsPage);
    await screen.findByText('Download execution completed');
    await fireEvent.click(screen.getByRole('button', { name: /Errori/ }));
    expect(screen.queryByText('Download execution completed')).toBeNull();

    await fireEvent.click(screen.getByRole('button', { name: /Tutti/ }));
    expect(screen.getByText('Download execution completed')).toBeTruthy();
    expect(screen.getByText('riga senza formato')).toBeTruthy();
  });

  it('ricerca case-insensitive filtra, conta ed evidenzia', async () => {
    const { container } = render(LogsPage);
    await screen.findByText('Download execution completed');

    const campo = screen.getByRole('searchbox', { name: 'Cerca nei log' });
    await fireEvent.input(campo, { target: { value: 'PSYREN' } });

    expect(container.querySelectorAll('.log-row')).toHaveLength(1);
    expect(screen.getByText('Psyren')).toBeTruthy();
    expect(screen.queryByText('Download execution completed')).toBeNull();
    expect(screen.getByRole('button', { name: /Tutti/ }).textContent).toContain('1');
    expect(screen.getByRole('button', { name: /Info/ }).textContent).toContain('0');
    expect(container.querySelectorAll('mark')).toHaveLength(1);
    expect(container.querySelector('mark').textContent).toBe('Psyren');
  });

  it('stato vuoto con filtri e Azzera filtri', async () => {
    render(LogsPage);
    await screen.findByText('Download execution completed');

    await fireEvent.input(screen.getByRole('searchbox', { name: 'Cerca nei log' }), {
      target: { value: 'zanzara' },
    });
    expect(screen.getByText('Nessun log corrisponde ai filtri.')).toBeTruthy();

    await fireEvent.click(screen.getByRole('button', { name: 'Azzera filtri' }));
    expect(screen.getByText('Download execution completed')).toBeTruthy();
  });

  it('nessun log disponibile senza filtri', async () => {
    logsMock.mockResolvedValue({ lines: [] });
    render(LogsPage);
    expect(await screen.findByText('Nessun log disponibile.')).toBeTruthy();
  });

  it('tab Controlli mostra il riepilogo e nasconde il viewer', async () => {
    render(LogsPage);
    await screen.findByText('Download execution completed');

    await fireEvent.click(screen.getByRole('tab', { name: 'Controlli' }));
    expect(screen.queryByRole('log')).toBeNull();
    expect(screen.getByText(/I controlli senza novità non richiedono azione/)).toBeTruthy();
    expect(await screen.findByText(
      'Nessun controllo in coda. I controlli completati non richiedono azione.'
    )).toBeTruthy();
    expect(queueListMock).toHaveBeenCalled();

    const prima = logsMock.mock.calls.length;
    await fireEvent.click(screen.getByRole('tab', { name: 'Log' }));
    expect(await screen.findByText('Download execution completed')).toBeTruthy();
    await waitFor(() => expect(logsMock.mock.calls.length).toBe(prima + 1));
  });

  it('errore iniziale: alert e Riprova ricarica', async () => {
    logsMock.mockRejectedValueOnce(new Error('offline'));
    render(LogsPage);
    expect(await screen.findByText('offline')).toBeTruthy();
    expect(screen.getByRole('alert')).toBeTruthy();

    await fireEvent.click(screen.getByRole('button', { name: 'Riprova' }));
    expect(await screen.findByText('Download execution completed')).toBeTruthy();
  });

  it('selettore righe ricarica con il nuovo limite', async () => {
    render(LogsPage);
    await screen.findByText('Download execution completed');

    const combo = screen.getByRole('combobox', { name: 'Righe da mostrare' });
    expect(combo.textContent).toContain('500 righe');
    await fireEvent.click(combo);
    await fireEvent.click(screen.getByRole('option', { name: '100 righe' }));
    await waitFor(() => expect(logsMock).toHaveBeenLastCalledWith(100));
  });

  it('Copia e Scarica funzionano sulle righe filtrate', async () => {
    const descrittore = Object.getOwnPropertyDescriptor(navigator, 'clipboard');
    const writeText = vi.fn().mockResolvedValue();
    Object.defineProperty(navigator, 'clipboard', {
      configurable: true,
      value: { writeText },
    });
    const createObjectURL = vi.fn(() => 'blob:log');
    const revokeObjectURL = vi.fn();
    const origCreate = URL.createObjectURL;
    const origRevoke = URL.revokeObjectURL;
    URL.createObjectURL = createObjectURL;
    URL.revokeObjectURL = revokeObjectURL;
    const click = vi.spyOn(HTMLAnchorElement.prototype, 'click').mockImplementation(() => {});
    try {
      render(LogsPage);
      await screen.findByText('Download execution completed');

      await fireEvent.click(screen.getByRole('button', { name: /Errori/ }));
      await fireEvent.click(screen.getByRole('button', { name: 'Copia' }));
      await waitFor(() => expect(writeText).toHaveBeenCalledWith(SAMPLE[2]));
      expect(await screen.findByText('Copiato')).toBeTruthy();

      await fireEvent.click(screen.getByRole('button', { name: 'Scarica' }));
      expect(createObjectURL).toHaveBeenCalledTimes(1);
      expect(revokeObjectURL).toHaveBeenCalledWith('blob:log');
      expect(click).toHaveBeenCalledTimes(1);
    } finally {
      click.mockRestore();
      URL.createObjectURL = origCreate;
      URL.revokeObjectURL = origRevoke;
      if (descrittore) Object.defineProperty(navigator, 'clipboard', descrittore);
      else delete navigator.clipboard;
    }
  });

  it('nessuna etichetta inglese nelle parti visibili', async () => {
    render(LogsPage);
    await screen.findByText('Download execution completed');
    const testo = document.body.textContent;
    expect(testo).not.toContain('Server Logs');
    expect(testo).not.toContain('Refresh');
    expect(testo).not.toContain('lines');
    expect(screen.getByRole('tab', { name: 'Log' })).toBeTruthy();
    expect(screen.getByRole('tab', { name: 'Controlli' })).toBeTruthy();
  });

  it('massimo 1500 righe renderizzate con hint di troncamento', async () => {
    const tante = Array.from({ length: 1600 }, (_, i) =>
      riga('2026-11-08 18:33:05', 'INFO', `riga numero ${i}`)
    );
    logsMock.mockResolvedValue({ lines: tante });
    const { container } = render(LogsPage);
    expect(await screen.findByText(/Mostro le ultime 1500 righe filtrate/)).toBeTruthy();
    expect(container.querySelectorAll('.log-row')).toHaveLength(1500);
  });

  it('polling 2s solo con Segui attivo (fake timers)', async () => {
    vi.useFakeTimers();
    try {
      render(LogsPage);
      await vi.advanceTimersByTimeAsync(0);
      expect(logsMock).toHaveBeenCalledTimes(1);

      await vi.advanceTimersByTimeAsync(2000);
      expect(logsMock).toHaveBeenCalledTimes(2);

      const segui = screen.getByRole('button', { name: 'Segui' });
      expect(segui.getAttribute('aria-pressed')).toBe('true');
      await fireEvent.click(segui);
      expect(segui.getAttribute('aria-pressed')).toBe('false');

      await vi.advanceTimersByTimeAsync(4000);
      expect(logsMock).toHaveBeenCalledTimes(2);

      await fireEvent.click(segui);
      expect(segui.getAttribute('aria-pressed')).toBe('true');
      await vi.advanceTimersByTimeAsync(0);
      expect(logsMock).toHaveBeenCalledTimes(3);
      await vi.advanceTimersByTimeAsync(2000);
      expect(logsMock).toHaveBeenCalledTimes(4);
    } finally {
      cleanup();
      vi.useRealTimers();
    }
  });

  it('in Controlli il polling del log è fermo', async () => {
    vi.useFakeTimers();
    try {
      render(LogsPage);
      await vi.advanceTimersByTimeAsync(0);
      expect(logsMock).toHaveBeenCalledTimes(1);

      await fireEvent.click(screen.getByRole('tab', { name: 'Controlli' }));
      await vi.advanceTimersByTimeAsync(0);
      const chiamate = logsMock.mock.calls.length;
      await vi.advanceTimersByTimeAsync(4000);
      expect(logsMock).toHaveBeenCalledTimes(chiamate);
    } finally {
      cleanup();
      vi.useRealTimers();
    }
  });

  it('pausa su scroll: accumula i nuovi e la pill torna in fondo', async () => {
    vi.useFakeTimers();
    try {
      const { container } = render(LogsPage);
      await vi.advanceTimersByTimeAsync(0);

      const vp = container.querySelector('.log-viewport');
      Object.defineProperty(vp, 'scrollHeight', { configurable: true, value: 1000 });
      Object.defineProperty(vp, 'clientHeight', { configurable: true, value: 400 });
      Object.defineProperty(vp, 'scrollTop', {
        configurable: true,
        writable: true,
        value: 100,
      });
      await fireEvent.scroll(vp);

      const nuova = riga('2026-11-08 18:33:09', 'INFO', 'Nuova riga arrivata');
      logsMock.mockResolvedValue({ lines: [...SAMPLE, nuova] });
      await vi.advanceTimersByTimeAsync(2000);

      const pill = screen.getByRole('button', { name: /1 nuovo · Torna in fondo/ });
      expect(pill).toBeTruthy();
      await fireEvent.click(pill);
      expect(screen.queryByRole('button', { name: /Torna in fondo/ })).toBeNull();
      expect(screen.getByRole('button', { name: 'Segui' }).getAttribute('aria-pressed')).toBe(
        'true'
      );
    } finally {
      cleanup();
      vi.useRealTimers();
    }
  });

  it('il polling salta quando la pagina è nascosta', async () => {
    vi.useFakeTimers();
    let nascosto = true;
    const descrittore = Object.getOwnPropertyDescriptor(document, 'hidden');
    Object.defineProperty(document, 'hidden', {
      configurable: true,
      get: () => nascosto,
    });
    try {
      render(LogsPage);
      await vi.advanceTimersByTimeAsync(0);
      expect(logsMock).toHaveBeenCalledTimes(1);

      await vi.advanceTimersByTimeAsync(2000);
      expect(logsMock).toHaveBeenCalledTimes(1);

      nascosto = false;
      await vi.advanceTimersByTimeAsync(2000);
      expect(logsMock).toHaveBeenCalledTimes(2);
    } finally {
      cleanup();
      if (descrittore) Object.defineProperty(document, 'hidden', descrittore);
      else delete document.hidden;
      vi.useRealTimers();
    }
  });
});
