import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen, fireEvent, waitFor } from '@testing-library/svelte';

const { configGetMock, configSetMock, thumbsMock } = vi.hoisted(() => ({
  configGetMock: vi.fn(), configSetMock: vi.fn(), thumbsMock: vi.fn(),
}));
vi.mock('../api.js', () => ({
  BASE: '',
  posterUrl: () => '/poster',
  posterSrcSet: () => '',
  api: {
    config: {
      get: (...args) => configGetMock(...args),
      set: (...args) => configSetMock(...args),
    },
    cache: {
      thumbs: (...args) => thumbsMock(...args),
      clearThumbs: () => Promise.resolve({ removedFiles: 0, freedBytes: 0 }),
    },
  },
}));

import ConfigPage from './ConfigPage.svelte';

// happy-dom non ha la Web Animations API usata da transition:fly:
// stub minimo col protocollo onfinish (copiato da DashboardPage.test.js).
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

describe('ConfigPage controlli automatici', () => {
  beforeEach(() => {
    vi.clearAllMocks();
    localStorage.clear();
    thumbsMock.mockResolvedValue(null);
  });

  it('default con backend vuoto: schedulazione ON/15, notifiche OFF', async () => {
    configGetMock.mockResolvedValue({ config: {} });
    render(ConfigPage);
    await waitFor(() => expect(screen.getByLabelText('Controllo periodico nuovi episodi')).toBeTruthy());
    expect(screen.getByLabelText('Controllo periodico nuovi episodi').checked).toBe(true);
    expect(screen.getByLabelText('Ogni quanti minuti').value).toBe('15');
    expect(screen.getByLabelText('Notifiche nuovi episodi').checked).toBe(false);
  });

  it('valori backend riflessi e salvati', async () => {
    configGetMock.mockResolvedValue({
      config: {
        scheduling: { abilitato: false, intervalloMinuti: 60 },
        notifiche: { abilitate: true, sorgente: 'backend' },
      },
    });
    render(ConfigPage);
    await waitFor(() => expect(screen.getByLabelText('Ogni quanti minuti').value).toBe('60'));
    expect(screen.getByLabelText('Controllo periodico nuovi episodi').checked).toBe(false);
    // Intervallo disabilitato quando la schedulazione è spenta.
    expect(screen.getByLabelText('Ogni quanti minuti').disabled).toBe(true);

    await fireEvent.click(screen.getByText('Salva Modifiche'));
    await waitFor(() => expect(configSetMock).toHaveBeenCalled());
    const inviato = configSetMock.mock.calls[0][0];
    expect(inviato.scheduling).toEqual({ abilitato: false, intervalloMinuti: 60 });
    expect(inviato.notifiche).toEqual({ abilitate: true, sorgente: 'backend' });
  });

  it('merge profondo: backend senza sezioni non perde i default', async () => {
    configGetMock.mockResolvedValue({ config: { convert_to_h265: false } });
    render(ConfigPage);
    await waitFor(() => expect(screen.getByLabelText('Ogni quanti minuti').value).toBe('15'));
    expect(screen.getByLabelText('Controllo periodico nuovi episodi').checked).toBe(true);
  });
});
