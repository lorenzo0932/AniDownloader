import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen, fireEvent, waitFor } from '@testing-library/svelte';
import DiscoverPage from './DiscoverPage.svelte';

const pagina1 = {
  success: true,
  items: [
    { titolo: 'Naruto Shippuden', pageUrl: '/play/naruto-shippuden.v3U8a', posterUrl: 'https://img.animeworld.ac/locandine/abc.png', tooltipId: '193' },
    { titolo: 'Boruto', pageUrl: '/play/boruto.lYBFQ', posterUrl: 'https://img.animeworld.ac/locandine/def.png', tooltipId: '162' },
  ],
  page: 1,
  maxPage: 2,
};

const pagina2 = {
  success: true,
  items: [
    { titolo: 'Naruto (ITA)', pageUrl: '/play/naruto-ita.x1', posterUrl: 'https://img.animeworld.ac/locandine/ghi.png', tooltipId: '99' },
  ],
  page: 2,
  maxPage: 2,
};

const tooltip = {
  success: true, trama: 'Trama di prova', voto: '8.5', stato: 'Finito',
  dataUscita: '01 Gennaio 2020', nomiAlternativi: '', generi: ['Azione', 'Avventura'],
};

function mockFetch() {
  return vi.fn(async (url) => {
    let body = pagina1;
    if (typeof url === 'string' && url.includes('/api/discover/tooltip')) body = tooltip;
    else if (typeof url === 'string' && url.includes('page=2')) body = pagina2;
    return { ok: true, status: 200, text: async () => JSON.stringify(body) };
  });
}

describe('DiscoverPage', () => {
  beforeEach(() => {
    vi.stubGlobal('fetch', mockFetch());
  });

  it('carica la prima pagina da sola e mostra la griglia', async () => {
    const { container } = render(DiscoverPage);
    await waitFor(() => expect(screen.getByText('Naruto Shippuden')).toBeTruthy());
    expect(container.querySelector('.discover-grid')).toBeTruthy();
    expect(screen.getByText('Boruto')).toBeTruthy();
  });

  it('mostra "carica altri" quando ci sono più pagine e accoda', async () => {
    const { container } = render(DiscoverPage);
    await waitFor(() => expect(screen.getByText('Naruto Shippuden')).toBeTruthy());
    const btn = screen.getByText(/Carica altri/);
    expect(btn).toBeTruthy();
    await fireEvent.click(btn);
    await waitFor(() => expect(screen.getByText('Naruto (ITA)')).toBeTruthy());
    // la prima pagina resta visibile (accoda, non sostituisce)
    expect(screen.getByText('Naruto Shippuden')).toBeTruthy();
    expect(container.querySelectorAll('.discover-card').length).toBe(3);
  });

  it('dettaglio lazy: click sulla card carica trama e generi', async () => {
    render(DiscoverPage);
    await waitFor(() => expect(screen.getByText('Naruto Shippuden')).toBeTruthy());
    await fireEvent.click(screen.getByText('Naruto Shippuden'));
    await waitFor(() => expect(screen.getByText('Trama di prova')).toBeTruthy());
    expect(screen.getByText(/Azione, Avventura/)).toBeTruthy();
  });

  it('Aggiungi: form precompilato con titolo e URL AnimeWorld', async () => {
    global.fetch = vi.fn(async (url, opts) => {
      if (typeof url === 'string' && url.includes('/api/discover/tooltip'))
        return { ok: true, status: 200, text: async () => JSON.stringify(tooltip) };
      if (opts && opts.method === 'POST')
        return { ok: true, status: 200, text: async () => JSON.stringify({ success: true }) };
      return { ok: true, status: 200, text: async () => JSON.stringify(pagina1) };
    });
    render(DiscoverPage);
    await waitFor(() => expect(screen.getByText('Naruto Shippuden')).toBeTruthy());
    await fireEvent.click(screen.getByText('Naruto Shippuden'));
    await waitFor(() => expect(screen.getByText('Trama di prova')).toBeTruthy());
    await fireEvent.click(screen.getByText('Aggiungi'));
    await waitFor(() => expect(screen.getByText('Aggiungi Nuova Serie')).toBeTruthy());
    // URL precompilato assoluto verso la pagina AnimeWorld
    expect(global.fetch).toHaveBeenCalled();
  });

  it('errore rete: messaggio dedicato, niente crash', async () => {
    global.fetch = vi.fn(async () => {
      throw new Error('rete giù');
    });
    render(DiscoverPage);
    await waitFor(() => expect(screen.getByText(/Ricerca fallita/)).toBeTruthy());
  });
});
