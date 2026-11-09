import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen, fireEvent, waitFor } from '@testing-library/svelte';

const { configGetMock, configSetMock, storageInfoMock, storageCleanupMock,
        browseListMock, browseMountsMock, browsePlacesMock } = vi.hoisted(() => ({
  configGetMock: vi.fn(),
  configSetMock: vi.fn(),
  storageInfoMock: vi.fn(),
  storageCleanupMock: vi.fn(),
  browseListMock: vi.fn(),
  browseMountsMock: vi.fn(),
  browsePlacesMock: vi.fn(),
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
    storage: {
      info: (...args) => storageInfoMock(...args),
      cleanup: (...args) => storageCleanupMock(...args),
    },
    browse: {
      list: (...args) => browseListMock(...args),
      mounts: (...args) => browseMountsMock(...args),
      places: (...args) => browsePlacesMock(...args),
    },
    queue: {
      list: () => Promise.resolve({ items: [], counts: {} }),
    },
    sse: () => ({ onmessage: null, close() {} }),
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

const STORAGE = {
  freeBytes: 500 * 1024 * 1024 * 1024,
  totalBytes: 1000 * 1024 * 1024 * 1024,
  cache: { files: 2, bytes: 1024 },
  partial: { files: 0, bytes: 0 },
  series: [],
};

function tab(name) {
  return screen.getByRole('tab', { name });
}

describe('ConfigPage — tab', () => {
  beforeEach(() => {
    vi.clearAllMocks();
    localStorage.clear();
    storageInfoMock.mockResolvedValue(STORAGE);
    storageCleanupMock.mockResolvedValue({ success: true, removedFiles: 0, freedBytes: 0 });
    browseListMock.mockResolvedValue({ path: '/tmp', entries: [] });
    browseMountsMock.mockResolvedValue({ mounts: [] });
    browsePlacesMock.mockResolvedValue({ places: [] });
  });

  it('default: tab Generali attiva, Aspetto visibile, Automazione nascosta', async () => {
    configGetMock.mockResolvedValue({ config: {} });
    render(ConfigPage);

    const generale = await screen.findByRole('tab', { name: 'Generali' });
    expect(generale.getAttribute('aria-selected')).toBe('true');
    expect(generale.getAttribute('aria-controls')).toBe('settings-panel-generali');
    expect(generale.getAttribute('tabindex')).toBe('0');

    // I pannelli dei tab non attivi restano montati ma `hidden`.
    expect(screen.getByRole('tabpanel', { name: 'Generali' })).toBeTruthy();
    expect(screen.queryByRole('tabpanel', { name: 'Automazione' })).toBeNull();
    expect(screen.queryByRole('tabpanel', { name: 'Sistema' })).toBeNull();

    expect(screen.getByText('Aspetto')).toBeTruthy();
    expect(screen.getByText('Comportamento e Avvisi')).toBeTruthy();
    expect(screen.getByRole('button', { name: 'Salva Modifiche' })).toBeTruthy();
    expect(screen.getByRole('button', { name: 'Annulla' })).toBeTruthy();
  });

  it('Automazione: default scheduling ON/15, notifiche OFF', async () => {
    configGetMock.mockResolvedValue({ config: {} });
    render(ConfigPage);
    await screen.findByRole('tab', { name: 'Generali' });

    await fireEvent.click(tab('Automazione'));
    expect(tab('Automazione').getAttribute('aria-selected')).toBe('true');
    expect(screen.getByRole('tabpanel', { name: 'Automazione' })).toBeTruthy();
    expect(screen.queryByRole('tabpanel', { name: 'Generali' })).toBeNull();

    expect(screen.getByLabelText('Controllo periodico nuovi episodi').checked).toBe(true);
    expect(screen.getByLabelText('Ogni quanti minuti').value).toBe('15');
    expect(screen.getByLabelText('Notifiche nuovi episodi').checked).toBe(false);
    // Il footer Salva resta disponibile nei tab di preferenza.
    expect(screen.getByRole('button', { name: 'Salva Modifiche' })).toBeTruthy();
  });

  it('Download: burst ON di default e due gruppi nel pannello', async () => {
    configGetMock.mockResolvedValue({ config: {} });
    render(ConfigPage);
    await screen.findByRole('tab', { name: 'Generali' });

    await fireEvent.click(tab('Download'));
    expect(screen.getByRole('tabpanel', { name: 'Download' })).toBeTruthy();
    expect(screen.getByLabelText('Download manuali con priorità massima (burst)').checked).toBe(true);
    expect(screen.getByText('Gestione File e Percorsi')).toBeTruthy();
    expect(screen.getByText('Codifica e Prestazioni Video')).toBeTruthy();
  });

  it('valori backend riflessi e salvati dal tab Automazione', async () => {
    configGetMock.mockResolvedValue({
      config: {
        scheduling: { abilitato: false, intervalloMinuti: 60 },
        notifiche: { abilitate: true, sorgente: 'backend' },
      },
    });
    render(ConfigPage);
    await screen.findByRole('tab', { name: 'Generali' });
    await fireEvent.click(tab('Automazione'));

    await waitFor(() => expect(screen.getByLabelText('Ogni quanti minuti').value).toBe('60'));
    expect(screen.getByLabelText('Controllo periodico nuovi episodi').checked).toBe(false);
    // Intervallo disabilitato quando la schedulazione è spenta.
    expect(screen.getByLabelText('Ogni quanti minuti').disabled).toBe(true);

    await fireEvent.click(screen.getByRole('button', { name: 'Salva Modifiche' }));
    await waitFor(() => expect(configSetMock).toHaveBeenCalled());
    const inviato = configSetMock.mock.calls[0][0];
    expect(inviato.scheduling).toEqual({ abilitato: false, intervalloMinuti: 60 });
    expect(inviato.notifiche).toEqual({ abilitate: true, sorgente: 'backend' });
  });

  it('merge profondo: backend senza sezioni non perde i default', async () => {
    configGetMock.mockResolvedValue({ config: { convert_to_h265: false } });
    render(ConfigPage);
    await screen.findByRole('tab', { name: 'Generali' });
    await fireEvent.click(tab('Automazione'));

    await waitFor(() => expect(screen.getByLabelText('Ogni quanti minuti').value).toBe('15'));
    expect(screen.getByLabelText('Controllo periodico nuovi episodi').checked).toBe(true);
  });

  it('Sistema: mostra StoragePanel, nasconde form e footer', async () => {
    configGetMock.mockResolvedValue({ config: {} });
    render(ConfigPage);
    await screen.findByRole('tab', { name: 'Generali' });

    await fireEvent.click(tab('Sistema'));
    expect(tab('Sistema').getAttribute('aria-selected')).toBe('true');
    expect(screen.getByRole('tabpanel', { name: 'Sistema' })).toBeTruthy();

    // StoragePanel montato fuori dal form, con i suoi dati.
    expect(await screen.findByText('Spazio e pulizia')).toBeTruthy();
    await waitFor(() => expect(storageInfoMock).toHaveBeenCalled());

    // Le preferenze sono nascoste e il footer Salva/Annulla non esiste.
    expect(screen.queryByRole('button', { name: 'Salva Modifiche' })).toBeNull();
    expect(screen.queryByRole('button', { name: 'Annulla' })).toBeNull();
    expect(screen.queryByRole('tabpanel', { name: 'Generali' })).toBeNull();
    // Niente “Manutenzione” duplicata: la cache vive solo in Sistema.
    expect(screen.queryByText('Manutenzione')).toBeNull();
  });

  it('modifiche non salvate sopravvivono al cambio tab', async () => {
    configGetMock.mockResolvedValue({ config: {} });
    render(ConfigPage);
    await screen.findByRole('tab', { name: 'Generali' });

    const stop = screen.getByLabelText('Mostra conferma prima di interrompere un download');
    expect(stop.checked).toBe(true);
    await fireEvent.click(stop);
    expect(stop.checked).toBe(false);

    await fireEvent.click(tab('Download'));
    const jsonPath = screen.getByLabelText('File Database Serie (JSON):');
    await fireEvent.input(jsonPath, { target: { value: '/tmp/serie.json' } });
    expect(jsonPath.value).toBe('/tmp/serie.json');

    await fireEvent.click(tab('Automazione'));
    await fireEvent.click(tab('Generali'));

    // Form sempre montato: i valori non salvati sono ancora lì.
    expect(screen.getByLabelText('Mostra conferma prima di interrompere un download').checked).toBe(false);
    await fireEvent.click(tab('Download'));
    expect(screen.getByLabelText('File Database Serie (JSON):').value).toBe('/tmp/serie.json');
    expect(configSetMock).not.toHaveBeenCalled();
  });

  it('nessun campo output_dir: resta solo il picker del file JSON', async () => {
    configGetMock.mockResolvedValue({ config: {} });
    render(ConfigPage);
    await screen.findByRole('tab', { name: 'Generali' });
    await fireEvent.click(tab('Download'));

    // La cartella di output non esiste più: i download usano series.path.
    expect(screen.queryByLabelText('Cartella di Destinazione (Output):')).toBeNull();
    expect(document.querySelector('#output_dir')).toBeNull();

    // L'unico picker rimasto è quello del database serie.
    expect(document.querySelectorAll('.btn-browse')).toHaveLength(1);
    await fireEvent.click(screen.getByTitle('Sfoglia'));
    expect(await screen.findByText('Scegli file JSON')).toBeTruthy();
  });

  it('tastiera: frecce muovono tab attivo, roving tabindex e focus', async () => {
    configGetMock.mockResolvedValue({ config: {} });
    render(ConfigPage);
    await screen.findByRole('tab', { name: 'Generali' });

    const generale = tab('Generali');
    generale.focus();
    await fireEvent.keyDown(generale, { key: 'ArrowRight' });

    const download = tab('Download');
    expect(download.getAttribute('aria-selected')).toBe('true');
    expect(download.getAttribute('tabindex')).toBe('0');
    expect(tab('Generali').getAttribute('tabindex')).toBe('-1');
    expect(document.activeElement).toBe(download);

    await fireEvent.keyDown(download, { key: 'ArrowLeft' });
    expect(tab('Generali').getAttribute('aria-selected')).toBe('true');
    expect(document.activeElement).toBe(tab('Generali'));
  });

  it('errore al primo caricamento: mostra errore senza renderizzare un form incompleto', async () => {
    configGetMock.mockRejectedValue(new Error('Configurazione non disponibile'));
    render(ConfigPage);
    expect(await screen.findByText('Configurazione non disponibile')).toBeTruthy();
    expect(screen.queryByText('Salva Modifiche')).toBeNull();
    expect(configSetMock).not.toHaveBeenCalled();
  });
});
