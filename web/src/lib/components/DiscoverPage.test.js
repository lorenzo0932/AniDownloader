import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen, fireEvent, waitFor } from '@testing-library/svelte';
import DiscoverPage from './DiscoverPage.svelte';

// happy-dom non ha la Web Animations API usata da animate:flip e
// transition: (collezione + modale): stub minimo come negli altri test.
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

const ID_NARUTO = 'animeW_scraper:/play/naruto-shippuden.v3U8a';
const ID_BORUTO = 'animeW_scraper:/play/boruto.lYBFQ';
const ID_NARUTO_ITA = 'animeW_scraper:/play/naruto-ita.x1';

const pagina1 = {
  success: true,
  items: [
    { titolo: 'Naruto Shippuden', pageUrl: '/play/naruto-shippuden.v3U8a', posterUrl: 'https://img.animeworld.ac/locandine/abc.png', tooltipId: '193', identity: ID_NARUTO },
    { titolo: 'Boruto', pageUrl: '/play/boruto.lYBFQ', posterUrl: 'https://img.animeworld.ac/locandine/def.png', tooltipId: '162', identity: ID_BORUTO },
  ],
  page: 1,
  maxPage: 2,
};

const pagina2 = {
  success: true,
  items: [
    { titolo: 'Naruto (ITA)', pageUrl: '/play/naruto-ita.x1', posterUrl: 'https://img.animeworld.ac/locandine/ghi.png', tooltipId: '99', identity: ID_NARUTO_ITA },
  ],
  page: 2,
  maxPage: 2,
};

const tooltip = {
  success: true, trama: 'Trama di prova', voto: '8.5', stato: 'Finito',
  dataUscita: '01 Gennaio 2020', nomiAlternativi: '', generi: ['Azione', 'Avventura'],
};

// Gestite: Boruto già in gestione, Naruto no.
const gestite = {
  success: true,
  series: [{ name: 'Boruto', identity: ID_BORUTO, series_page_url: 'https://www.animeworld.ac/play/boruto.lYBFQ' }],
};

function mockFetch({ postImpl = null, tooltipImpl = null, seriesImpl = null } = {}) {
  const postBodies = [];
  const fn = vi.fn(async (url, opts) => {
    if (typeof url === 'string' && url.includes('/api/discover/tooltip')) {
      const body = tooltipImpl ? await tooltipImpl(url) : tooltip;
      return { ok: true, status: 200, text: async () => JSON.stringify(body) };
    }
    if (opts && opts.method === 'POST') {
      postBodies.push(JSON.parse(opts.body));
      if (postImpl) return postImpl(url, opts);
      return { ok: true, status: 200, text: async () => JSON.stringify({ success: true, index: 5, identity: ID_NARUTO }) };
    }
    if (typeof url === 'string' && url.includes('/api/series')) {
      const body = seriesImpl ? await seriesImpl(url) : gestite;
      return { ok: true, status: 200, text: async () => JSON.stringify(body) };
    }
    if (typeof url === 'string' && url.includes('page=2'))
      return { ok: true, status: 200, text: async () => JSON.stringify(pagina2) };
    return { ok: true, status: 200, text: async () => JSON.stringify(pagina1) };
  });
  fn.postBodies = postBodies;
  return fn;
}

describe('DiscoverPage', () => {
  beforeEach(() => {
    vi.stubGlobal('fetch', mockFetch());
    localStorage.clear();
  });

  it('carica la prima pagina da sola e mostra la griglia condivisa', async () => {
    const { container } = render(DiscoverPage);
    await waitFor(() => expect(screen.getByText('Naruto Shippuden')).toBeTruthy());
    expect(container.querySelector('.series-grid')).toBeTruthy();
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
    expect(container.querySelectorAll('.series-grid .series-card')).toHaveLength(3);
  });

  it('dettaglio lazy: click sulla card carica trama e generi', async () => {
    render(DiscoverPage);
    await waitFor(() => expect(screen.getByText('Naruto Shippuden')).toBeTruthy());
    await fireEvent.click(screen.getByText('Naruto Shippuden'));
    await waitFor(() => expect(screen.getByText('Trama di prova')).toBeTruthy());
    expect(screen.getByText(/Azione, Avventura/)).toBeTruthy();
  });

  it('badge: serie gestita con "In gestione" e Aggiungi disabilitato', async () => {
    const { container } = render(DiscoverPage);
    await waitFor(() => expect(screen.getByText('Boruto')).toBeTruthy());
    // Badge testuale (non solo colore) su card e bottone Aggiungi disabilitato.
    const badges = container.querySelectorAll('.card-managed');
    expect(badges.length).toBeGreaterThan(0);
    const borutoCard = screen.getByText('Boruto').closest('.series-card');
    expect(borutoCard.querySelector('[aria-label="Già in gestione"]')).toBeTruthy();
    expect(borutoCard.querySelector('[aria-label="Già in gestione"]').disabled).toBe(true);
    // Naruto non gestita: Aggiungi abilitato.
    const narutoCard = screen.getByText('Naruto Shippuden').closest('.series-card');
    expect(narutoCard.querySelector('[aria-label="Aggiungi"]')).toBeTruthy();
    expect(narutoCard.querySelector('[aria-label="Aggiungi"]').disabled).toBe(false);
  });

  it('dettaglio gestito: badge e Aggiungi disabilitato anche nel modale', async () => {
    render(DiscoverPage);
    await waitFor(() => expect(screen.getByText('Boruto')).toBeTruthy());
    await fireEvent.click(screen.getByText('Boruto'));
    await waitFor(() => expect(screen.getByText('Trama di prova')).toBeTruthy());
    // Badge sia sulla card sia nel modale (testo, non solo colore).
    expect(screen.getAllByText('In gestione').length).toBeGreaterThanOrEqual(2);
    const btn = screen.getByRole('button', { name: 'In gestione' });
    expect(btn.disabled).toBe(true);
  });

  it('Aggiungi serie non gestita: form precompilato, POST e notice', async () => {
    global.fetch = mockFetch();
    render(DiscoverPage);
    await waitFor(() => expect(screen.getByText('Naruto Shippuden')).toBeTruthy());
    await fireEvent.click(screen.getByText('Naruto Shippuden'));
    await waitFor(() => expect(screen.getByText('Trama di prova')).toBeTruthy());
    await fireEvent.click(screen.getByText('Aggiungi'));
    await waitFor(() => expect(screen.getByText('Aggiungi Nuova Serie')).toBeTruthy());
    const pathInput = screen.getByPlaceholderText('/path/to/series');
    await fireEvent.input(pathInput, { target: { value: '/tmp/serie-prova' } });
    await fireEvent.click(screen.getByRole('button', { name: /Salva Modifiche/ }));
    await waitFor(() => expect(global.fetch.postBodies?.length ?? 1).toBe(1));
    await waitFor(() => expect(screen.getByText(/aggiunta: la troverai in Gestione Serie/)).toBeTruthy());
  });

  it('Aggiungi senza spunta metadati: POST con fetch_metadata=false', async () => {
    global.fetch = mockFetch();
    render(DiscoverPage);
    await waitFor(() => expect(screen.getByText('Naruto Shippuden')).toBeTruthy());
    await fireEvent.click(screen.getByText('Naruto Shippuden'));
    await waitFor(() => expect(screen.getByText('Trama di prova')).toBeTruthy());
    await fireEvent.click(screen.getByText('Aggiungi'));
    await waitFor(() => expect(screen.getByText('Aggiungi Nuova Serie')).toBeTruthy());
    // toglie la spunta (default attiva) e salva con path compilato a mano
    await fireEvent.click(screen.getByRole('checkbox', { name: /Scarica locandina e metadati/ }));
    const pathInput = screen.getByPlaceholderText('/path/to/series');
    await fireEvent.input(pathInput, { target: { value: '/tmp/serie-prova' } });
    await fireEvent.click(screen.getByRole('button', { name: /Salva Modifiche/ }));
    await waitFor(() => expect(global.fetch.postBodies.length).toBe(1));
    expect(global.fetch.postBodies[0].fetch_metadata).toBe(false);
  });

  it('doppio invio: un solo POST', async () => {
    global.fetch = mockFetch();
    render(DiscoverPage);
    await waitFor(() => expect(screen.getByText('Naruto Shippuden')).toBeTruthy());
    await fireEvent.click(screen.getByText('Naruto Shippuden'));
    await waitFor(() => expect(screen.getByText('Trama di prova')).toBeTruthy());
    await fireEvent.click(screen.getByText('Aggiungi'));
    await waitFor(() => expect(screen.getByText('Aggiungi Nuova Serie')).toBeTruthy());
    const pathInput = screen.getByPlaceholderText('/path/to/series');
    await fireEvent.input(pathInput, { target: { value: '/tmp/serie-prova' } });
    const salva = screen.getByRole('button', { name: /Salva Modifiche/ });
    await fireEvent.click(salva);
    await fireEvent.click(salva);
    await waitFor(() => expect(screen.getByText(/aggiunta: la troverai in Gestione Serie/)).toBeTruthy());
    expect(global.fetch.postBodies.length).toBe(1);
  });

  it('409 duplicato: messaggio dedicato e badge, nessun falso successo', async () => {
    global.fetch = mockFetch({
      postImpl: () => ({
        ok: false, status: 409,
        text: async () => JSON.stringify({ error: 'Serie già in gestione', code: 409, error_code: 'series_already_managed', identity: ID_NARUTO }),
      }),
    });
    const { container } = render(DiscoverPage);
    await waitFor(() => expect(screen.getByText('Naruto Shippuden')).toBeTruthy());
    await fireEvent.click(screen.getByText('Naruto Shippuden'));
    await waitFor(() => expect(screen.getByText('Trama di prova')).toBeTruthy());
    await fireEvent.click(screen.getByText('Aggiungi'));
    await waitFor(() => expect(screen.getByText('Aggiungi Nuova Serie')).toBeTruthy());
    const pathInput = screen.getByPlaceholderText('/path/to/series');
    await fireEvent.input(pathInput, { target: { value: '/tmp/serie-prova' } });
    await fireEvent.click(screen.getByRole('button', { name: /Salva Modifiche/ }));
    await waitFor(() => expect(screen.getByText(/già in gestione/)).toBeTruthy());
    // Form resta aperto (dati preservati), niente notice di successo.
    expect(screen.getByText('Aggiungi Nuova Serie')).toBeTruthy();
    expect(screen.queryByText(/la troverai in Gestione Serie/)).toBeNull();
    // Il conflitto noto mette il badge anche senza refetch riuscito.
    expect(container.querySelector('.card-managed')).toBeTruthy();
  });

  it('dettaglio in caricamento: skeleton stabile, poi trama (niente scatto)', async () => {
    let risolviTooltip;
    global.fetch = mockFetch({
      tooltipImpl: () => new Promise((risolvi) => { risolviTooltip = () => risolvi(tooltip); }),
    });
    const { container } = render(DiscoverPage);
    await waitFor(() => expect(screen.getByText('Naruto Shippuden')).toBeTruthy());
    await fireEvent.click(screen.getByText('Naruto Shippuden'));
    // In attesa: skeleton con status accessibile, dimensioni riservate.
    const skeleton = await screen.findByRole('status', { name: 'Caricamento dettagli' });
    expect(skeleton.classList.contains('detail-skeleton')).toBe(true);
    expect(container.querySelector('.detail-modal')).toBeTruthy();
    const content = container.querySelector('.discover-detail-content');
    expect(content.getAttribute('aria-busy')).toBe('true');
    risolviTooltip();
    await waitFor(() => expect(screen.getByText('Trama di prova')).toBeTruthy());
    expect(container.querySelector('.discover-detail-content')).toBe(content);
    expect(content.getAttribute('aria-busy')).toBe('false');
    expect(content.querySelector('.detail-meta-list')).toBeTruthy();
    expect(screen.queryByRole('status', { name: 'Caricamento dettagli' })).toBeNull();
  });

  it('tooltip fuori ordine: vince l\'ultimo dettaglio aperto', async () => {
    global.fetch = mockFetch({
      tooltipImpl: async (url) => {
        const id193 = url.includes('id=193');
        await new Promise((r) => setTimeout(r, id193 ? 60 : 5));
        return { ...tooltip, trama: id193 ? 'Trama Naruto' : 'Trama Boruto' };
      },
    });
    render(DiscoverPage);
    await waitFor(() => expect(screen.getByText('Naruto Shippuden')).toBeTruthy());
    await fireEvent.click(screen.getByText('Naruto Shippuden'));
    await fireEvent.click(screen.getByText('Boruto'));
    await waitFor(() => expect(screen.getByText('Trama Boruto')).toBeTruthy());
    // La risposta lenta di Naruto non deve sovrascrivere Boruto.
    await new Promise((r) => setTimeout(r, 120));
    expect(screen.queryByText('Trama Naruto')).toBeNull();
    expect(screen.getByText('Trama Boruto')).toBeTruthy();
  });

  it('gestite non disponibili: errore con Riprova, Aggiungi bloccato', async () => {
    global.fetch = mockFetch({
      seriesImpl: async () => { throw new Error('rete giù'); },
    });
    render(DiscoverPage);
    await waitFor(() => expect(screen.getByText('Naruto Shippuden')).toBeTruthy());
    await waitFor(() => expect(screen.getByText(/Elenco serie non disponibile/)).toBeTruthy());
    const narutoCard = screen.getByText('Naruto Shippuden').closest('.series-card');
    const btn = narutoCard.querySelector('[aria-label="Aggiungi"]');
    expect(btn.disabled).toBe(true);
    expect(btn.title).toMatch(/caricamento/i);
  });

  it('errore rete catalogo: messaggio dedicato, niente crash', async () => {
    global.fetch = vi.fn(async (url, opts) => {
      if (typeof url === 'string' && url.includes('/api/series'))
        return { ok: true, status: 200, text: async () => JSON.stringify(gestite) };
      throw new Error('rete giù');
    });
    render(DiscoverPage);
    await waitFor(() => expect(screen.getByText(/Ricerca fallita/)).toBeTruthy());
  });
});
