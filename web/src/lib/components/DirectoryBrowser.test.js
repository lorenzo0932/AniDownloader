import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen, fireEvent, waitFor, within } from '@testing-library/svelte';
import DirectoryBrowser from './DirectoryBrowser.svelte';
import { api } from '../api.js';

const ENTRIES = [
  { name: 'Alpha', path: '/media/Alpha', type: 'dir', mtime: 1000, size: 0 },
  { name: 'Beta', path: '/media/Beta', type: 'dir', mtime: 2000, size: 0 },
  { name: 'ep01.mkv', path: '/media/ep01.mkv', type: 'file', mtime: 3000, size: 900 },
];

function browseOk(overrides = {}) {
  return {
    success: true,
    entries: ENTRIES,
    path: '/media',
    parent: '/',
    ...overrides,
  };
}

function stubBrowser({ list = browseOk(), mounts = [], pinned = [], places = [] } = {}) {
  vi.spyOn(api.browse, 'list').mockResolvedValue(list);
  vi.spyOn(api.browse, 'mounts').mockResolvedValue({ success: true, mounts });
  vi.spyOn(api.browse, 'places').mockResolvedValue({ success: true, places });
  vi.spyOn(api.config, 'get').mockResolvedValue({ success: true, config: { pinned_paths: pinned } });
}

const props = { show: true, currentPath: '/media', onselect: vi.fn(), oncancel: vi.fn() };

describe('DirectoryBrowser', () => {
  beforeEach(() => {
    vi.restoreAllMocks();
  });

  it('show=false: non renderizza nulla', () => {
    stubBrowser();
    render(DirectoryBrowser, { ...props, show: false });
    expect(screen.queryByRole('dialog')).toBeNull();
  });

  it('show=true: elenca dir e file con il path corrente', async () => {
    stubBrowser();
    render(DirectoryBrowser, props);
    await waitFor(() => expect(screen.getByText('Beta')).toBeTruthy());
    expect(screen.getByText('Alpha')).toBeTruthy();
    expect(screen.getByText('ep01.mkv')).toBeTruthy();
    expect(api.browse.list).toHaveBeenCalledWith('/media', true);
  });

  it('ricerca: filtra in locale per nome, case-insensitive', async () => {
    stubBrowser();
    render(DirectoryBrowser, props);
    await waitFor(() => expect(screen.getByText('Beta')).toBeTruthy());

    await fireEvent.input(screen.getByLabelText('Cerca'), { target: { value: 'bet' } });
    await waitFor(() => expect(screen.queryByText('Alpha')).toBeNull());
    expect(screen.getByText('Beta')).toBeTruthy();
    // Anche i file sono filtrati: ep01.mkv non contiene "bet".
    expect(screen.queryByText('ep01.mkv')).toBeNull();
    // Nessuna chiamata di rete: il filtro è puramente client-side.
    expect(api.browse.list).toHaveBeenCalledTimes(1);
  });

  it('ricerca senza risultati: messaggio esplicito', async () => {
    stubBrowser();
    render(DirectoryBrowser, props);
    await waitFor(() => expect(screen.getByText('Beta')).toBeTruthy());
    await fireEvent.input(screen.getByLabelText('Cerca'), { target: { value: 'zzz' } });
    await waitFor(() => expect(screen.getByText(/Nessun elemento corrisponde/)).toBeTruthy());
  });

  it('ricerca: cambiando cartella si azzera (e' + ' per-cartella)', async () => {
    stubBrowser();
    render(DirectoryBrowser, props);
    await waitFor(() => expect(screen.getByText('Beta')).toBeTruthy());

    await fireEvent.input(screen.getByLabelText('Cerca'), { target: { value: 'zzz' } });
    await waitFor(() => expect(screen.getByText(/Nessun elemento corrisponde/)).toBeTruthy());

    // Entra in un'altra cartella ("su", sempre visibile): la ricerca non
    // deve filtrare anche la nuova.
    await fireEvent.click(screen.getByText('.. (su)'));
    await waitFor(() => expect(api.browse.list).toHaveBeenCalledWith('/', true));
    expect(screen.getByLabelText('Cerca').value).toBe('');
    await waitFor(() => expect(screen.getByText('Beta')).toBeTruthy());
  });

  it('dischi: elenca i mount e naviga al click', async () => {
    stubBrowser({ mounts: [{ name: 'Disco locale (C:)', path: 'C:\\' }] });
    render(DirectoryBrowser, props);
    await waitFor(() => expect(screen.getByText('Dischi')).toBeTruthy());
    expect(screen.getByText('Disco locale (C:)')).toBeTruthy();

    await fireEvent.click(screen.getByText('Disco locale (C:)'));
    await waitFor(() => expect(api.browse.list).toHaveBeenCalledWith('C:\\', true));
  });

  it('dischi: stanno in sidebar, non nel contenuto della cartella', async () => {
    stubBrowser({ mounts: [{ name: 'usb', path: '/media/usb' }] });
    const { container } = render(DirectoryBrowser, props);
    await waitFor(() => expect(screen.getByText('usb')).toBeTruthy());
    // Il contenuto mostra solo le voci della cartella, non i dischi.
    const main = container.querySelector('.browser-main');
    expect(main.textContent).not.toContain('usb');
    expect(main.textContent).toContain('Contenuto');
  });

  it('posizioni: elencate in sidebar e navigabili', async () => {
    stubBrowser({ places: [{ id: 'home', name: 'Home', path: '/home/user' }] });
    render(DirectoryBrowser, props);
    await waitFor(() => expect(screen.getByText('Posizioni')).toBeTruthy());
    expect(screen.getByText('Home')).toBeTruthy();

    await fireEvent.click(screen.getByText('Home'));
    await waitFor(() => expect(api.browse.list).toHaveBeenCalledWith('/home/user', true));
  });

  it('drawer: toggle apre/chiude, navigare dalla sidebar chiude', async () => {
    stubBrowser({ places: [{ id: 'home', name: 'Home', path: '/home/user' }] });
    const { container } = render(DirectoryBrowser, props);
    await waitFor(() => expect(screen.getByText('Beta')).toBeTruthy());

    const sidebar = container.querySelector('.browser-sidebar');
    expect(sidebar.classList.contains('open')).toBe(false);

    await fireEvent.click(screen.getByLabelText('Posizioni e dischi'));
    expect(sidebar.classList.contains('open')).toBe(true);

    await fireEvent.click(screen.getByText('Home'));
    await waitFor(() => expect(sidebar.classList.contains('open')).toBe(false));
  });

  it('preferiti: carica da config e la stella pinna il path corrente', async () => {
    stubBrowser({ pinned: ['/media/Vecchia'] });
    const setSpy = vi.spyOn(api.config, 'set').mockResolvedValue({ success: true });
    render(DirectoryBrowser, props);
    await waitFor(() => expect(screen.getByText('/media/Vecchia')).toBeTruthy());

    await fireEvent.click(screen.getByLabelText('Preferito'));
    await waitFor(() =>
      expect(setSpy).toHaveBeenCalledWith({ pinned_paths: ['/media', '/media/Vecchia'] })
    );
  });

  it('preferiti: la stella toglie un path gia pinnato', async () => {
    stubBrowser({ pinned: ['/media'] });
    const setSpy = vi.spyOn(api.config, 'set').mockResolvedValue({ success: true });
    render(DirectoryBrowser, props);
    // 'Preferiti' in sidebar = i pin sono stati caricati dalla config.
    await waitFor(() => expect(screen.getByText('Preferiti')).toBeTruthy());

    await fireEvent.click(screen.getByLabelText('Preferito'));
    await waitFor(() => expect(setSpy).toHaveBeenCalledWith({ pinned_paths: [] }));
  });

  it('su: usa il parent del server, senza splittare il path', async () => {
    stubBrowser();
    render(DirectoryBrowser, props);
    await waitFor(() => expect(screen.getByText('Beta')).toBeTruthy());

    await fireEvent.click(screen.getByText('.. (su)'));
    await waitFor(() => expect(api.browse.list).toHaveBeenCalledWith('/', true));
  });

  it('su disabilitato alla root (parent null)', async () => {
    stubBrowser({ list: browseOk({ path: '/', parent: null }) });
    render(DirectoryBrowser, props);
    await waitFor(() => expect(screen.getByText('Beta')).toBeTruthy());
    expect(screen.queryByText('.. (su)')).toBeNull();
  });

  it('creazione cartella: crea e ricarica', async () => {
    stubBrowser();
    const mkdir = vi.spyOn(api.browse, 'mkdir').mockResolvedValue({ success: true, path: '/media/Nuova' });
    render(DirectoryBrowser, props);
    await waitFor(() => expect(screen.getByText('Beta')).toBeTruthy());

    await fireEvent.click(screen.getByText('+ Cartella'));
    await fireEvent.input(screen.getByLabelText('Nuovo nome'), { target: { value: 'Nuova' } });
    await fireEvent.click(screen.getByText('Crea'));

    await waitFor(() => expect(mkdir).toHaveBeenCalledWith('/media', 'Nuova'));
    await waitFor(() => expect(api.browse.list).toHaveBeenCalledTimes(2));
  });

  it('creazione: nome con separatore rifiutata client-side', async () => {
    stubBrowser();
    const mkdir = vi.spyOn(api.browse, 'mkdir').mockResolvedValue({ success: true });
    render(DirectoryBrowser, props);
    await waitFor(() => expect(screen.getByText('Beta')).toBeTruthy());

    await fireEvent.click(screen.getByText('+ Cartella'));
    await fireEvent.input(screen.getByLabelText('Nuovo nome'), { target: { value: 'a/b' } });
    await fireEvent.click(screen.getByText('Crea'));

    await waitFor(() => expect(screen.getByText('Nome non valido')).toBeTruthy());
    expect(mkdir).not.toHaveBeenCalled();
  });

  it('creazione file: usa touch', async () => {
    stubBrowser();
    const touch = vi.spyOn(api.browse, 'touch').mockResolvedValue({ success: true });
    render(DirectoryBrowser, props);
    await waitFor(() => expect(screen.getByText('Beta')).toBeTruthy());

    await fireEvent.click(screen.getByText('+ File'));
    await fireEvent.input(screen.getByLabelText('Nuovo nome'), { target: { value: 'nuovo.mkv' } });
    await fireEvent.click(screen.getByText('Crea'));
    await waitFor(() => expect(touch).toHaveBeenCalledWith('/media', 'nuovo.mkv'));
  });

  it('elimina file: prima conferma poi remove non ricorsivo', async () => {
    stubBrowser();
    const remove = vi.spyOn(api.browse, 'remove').mockResolvedValue({ success: true });
    render(DirectoryBrowser, props);
    await waitFor(() => expect(screen.getByText('ep01.mkv')).toBeTruthy());

    await fireEvent.click(screen.getByLabelText('Elimina ep01.mkv'));
    await waitFor(() => expect(screen.getByText('Eliminare?')).toBeTruthy());
    await fireEvent.click(screen.getByText('Elimina'));

    await waitFor(() => expect(remove).toHaveBeenCalledWith('/media/ep01.mkv', false));
  });

  it('elimina cartella non vuota: 409 poi seconda conferma ricorsiva', async () => {
    stubBrowser();
    const remove = vi
      .spyOn(api.browse, 'remove')
      .mockRejectedValueOnce(Object.assign(new Error('La cartella non è vuota'), { status: 409, count: 3 }))
      .mockResolvedValueOnce({ success: true });
    render(DirectoryBrowser, props);
    await waitFor(() => expect(screen.getByText('Alpha')).toBeTruthy());

    await fireEvent.click(screen.getByLabelText('Elimina Alpha'));
    await waitFor(() => expect(screen.getByText('Eliminare?')).toBeTruthy());
    await fireEvent.click(screen.getByText('Elimina'));

    // Dopo il 409 compare il secondo dialogo, con il conteggio elementi.
    await waitFor(() => expect(screen.getByText('Eliminare tutto il contenuto?')).toBeTruthy());
    expect(screen.getByText(/3 elementi/)).toBeTruthy();

    await fireEvent.click(screen.getByText('Elimina tutto'));
    await waitFor(() => expect(remove).toHaveBeenLastCalledWith('/media/Alpha', true));
  });

  it('elimina: annulla il primo dialogo senza chiamare remove', async () => {
    stubBrowser();
    const remove = vi.spyOn(api.browse, 'remove').mockResolvedValue({ success: true });
    render(DirectoryBrowser, props);
    await waitFor(() => expect(screen.getByText('Alpha')).toBeTruthy());

    await fireEvent.click(screen.getByLabelText('Elimina Alpha'));
    await waitFor(() => expect(screen.getByText('Eliminare?')).toBeTruthy());
    // "Annulla" del dialogo di conferma, non quello del footer del picker.
    const confirmDialog = screen.getAllByRole('dialog').at(-1);
    await fireEvent.click(within(confirmDialog).getByText('Annulla'));

    await waitFor(() => expect(screen.queryByText('Eliminare?')).toBeNull());
    expect(remove).not.toHaveBeenCalled();
  });

  it('seleziona: ritorna il path corrente', async () => {
    stubBrowser();
    const onselect = vi.fn();
    render(DirectoryBrowser, { ...props, onselect });
    await waitFor(() => expect(screen.getByText('Beta')).toBeTruthy());
    await fireEvent.click(screen.getByText('Seleziona questa cartella'));
    expect(onselect).toHaveBeenCalledWith('/media');
  });

  it('errore di caricamento: mostrato all utente', async () => {
    vi.spyOn(api.browse, 'list').mockRejectedValue(new Error('Path inesistente'));
    vi.spyOn(api.browse, 'mounts').mockResolvedValue({ success: true, mounts: [] });
    vi.spyOn(api.config, 'get').mockResolvedValue({ success: true, config: {} });
    render(DirectoryBrowser, props);
    await waitFor(() => expect(screen.getByText('Path inesistente')).toBeTruthy());
  });

  it('preferiti: se il salvataggio fallisce, rollback allo stato precedente', async () => {
    stubBrowser({ pinned: [] });
    const setSpy = vi.spyOn(api.config, 'set').mockRejectedValue(new Error('Disco pieno'));
    render(DirectoryBrowser, props);
    await waitFor(() => expect(screen.getByText('Beta')).toBeTruthy());

    const star = screen.getByLabelText('Preferito');
    await fireEvent.click(star);
    await waitFor(() => expect(screen.getByText(/Salvataggio preferiti fallito/)).toBeTruthy());
    expect(setSpy).toHaveBeenCalledWith({ pinned_paths: ['/media'] });

    // Senza rollback il pin ottimistico resterebbe: il secondo click
    // toglierebbe invece di ri-aggiungere. Con rollback, riprova ad aggiungere.
    await fireEvent.click(star);
    await waitFor(() => expect(setSpy).toHaveBeenCalledTimes(2));
    expect(setSpy).toHaveBeenNthCalledWith(2, { pinned_paths: ['/media'] });
  });

  it('preferiti: se la rimozione fallisce, il pin resta', async () => {
    stubBrowser({ pinned: ['/media'] });
    const setSpy = vi.spyOn(api.config, 'set').mockRejectedValue(new Error('Disco pieno'));
    render(DirectoryBrowser, props);
    // 'Preferiti' in sidebar = i pin sono stati caricati dalla config.
    await waitFor(() => expect(screen.getByText('Preferiti')).toBeTruthy());

    const star = screen.getByLabelText('Preferito');
    await fireEvent.click(star);
    await waitFor(() => expect(screen.getByText(/Salvataggio preferiti fallito/)).toBeTruthy());
    expect(setSpy).toHaveBeenCalledWith({ pinned_paths: [] });

    // Con rollback il pin e' ancora li': il secondo click riprova a toglierlo.
    await fireEvent.click(star);
    await waitFor(() => expect(setSpy).toHaveBeenCalledTimes(2));
    expect(setSpy).toHaveBeenNthCalledWith(2, { pinned_paths: [] });
  });

  it('dischi: Aggiorna rilegge i mount (volumi inseriti a caldo)', async () => {
    const mountsSpy = vi
      .spyOn(api.browse, 'mounts')
      .mockResolvedValue({ success: true, mounts: [{ name: 'usb', path: '/media/usb' }] });
    vi.spyOn(api.browse, 'list').mockResolvedValue(browseOk());
    vi.spyOn(api.config, 'get').mockResolvedValue({ success: true, config: {} });
    render(DirectoryBrowser, props);
    await waitFor(() => expect(screen.getByText('usb')).toBeTruthy());
    expect(mountsSpy).toHaveBeenCalledTimes(1);

    await fireEvent.click(screen.getByText('Aggiorna'));
    await waitFor(() => expect(mountsSpy).toHaveBeenCalledTimes(2));
  });

  it('campo manuale: dopo la navigazione mostra il path normalizzato dal server', async () => {
    vi.spyOn(api.browse, 'list').mockResolvedValue(browseOk({ path: '/media' }));
    vi.spyOn(api.browse, 'mounts').mockResolvedValue({ success: true, mounts: [] });
    vi.spyOn(api.browse, 'places').mockResolvedValue({ success: true, places: [] });
    vi.spyOn(api.config, 'get').mockResolvedValue({ success: true, config: {} });
    render(DirectoryBrowser, { ...props, currentPath: '/media/' });
    await waitFor(() => expect(screen.getByText('Beta')).toBeTruthy());
    expect(screen.getByPlaceholderText('Inserisci percorso manualmente...').value).toBe('/media');
  });

  it('campo manuale: testo digitato durante il fetch non viene sovrascritto', async () => {
    stubBrowser();
    let resolveList;
    vi.spyOn(api.browse, 'list').mockImplementation(
      () => new Promise((r) => { resolveList = r; })
    );
    render(DirectoryBrowser, props);
    // Aspetta che il caricamento iniziale sia partito (fetch in volo).
    await waitFor(() => {
      if (!resolveList) throw new Error('fetch non partito');
    });
    // L'utente digita mentre il caricamento e' ancora in volo.
    await fireEvent.input(screen.getByPlaceholderText('Inserisci percorso manualmente...'), {
      target: { value: '/tmp/bozza' },
    });
    resolveList(browseOk());
    await waitFor(() => expect(screen.getByText('Beta')).toBeTruthy());
    expect(screen.getByPlaceholderText('Inserisci percorso manualmente...').value).toBe('/tmp/bozza');
  });
});
