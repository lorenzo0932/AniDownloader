import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen, fireEvent, within } from '@testing-library/svelte';

const { listMock, updateMock, addMock, removeMock } = vi.hoisted(() => ({
  listMock: vi.fn(), updateMock: vi.fn(), addMock: vi.fn(), removeMock: vi.fn(),
}));
vi.mock('../api.js', () => ({
  BASE: '',
  posterUrl: () => '/poster',
  posterSrcSet: () => '',
  api: {
    series: {
      list: (...args) => listMock(...args),
      update: (...args) => updateMock(...args),
      add: (...args) => addMock(...args),
      remove: (...args) => removeMock(...args),
    },
  },
}));

import DashboardPage from './DashboardPage.svelte';

// happy-dom non ha la Web Animations API usata da transition:/in:fly:
// stub minimo. Svelte crea una dummy-animation + una principale e attende
// `onfinish` su entrambe: va invocato in async dopo l'assegnazione,
// altrimenti gli outro non completano mai e i nodi restano (inert).
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

const VIEW_KEY = 'anidl.series.view';
const SORT_KEY = 'anidl.series.sort';

const seriesFixture = () => [
  { _file_index: 0, name: 'One Piece', path: '/media/one-piece', service: 'animeW_scraper', local_episode_count: 100, series_page_url: 'https://example.com/one-piece' },
  { _file_index: 1, name: 'Naruto', path: '/media/naruto', service: 'animeU_scraper', local_episode_count: 50, series_page_url: 'https://example.com/naruto' },
];

beforeEach(() => {
  localStorage.clear();
  document.body.style.overflow = '';
  listMock.mockReset();
  updateMock.mockReset();
  addMock.mockReset();
  removeMock.mockReset();
  // Array fresco a ogni chiamata: load() fa reverse() in place per
  // 'added'+desc e non deve inquinare i load successivi.
  listMock.mockImplementation(async () => ({ series: seriesFixture() }));
  vi.stubGlobal('fetch', vi.fn(async () => ({ ok: false })));
});

describe('DashboardPage vista', () => {
  it('default: vista large (griglia con colonne grandi)', async () => {
    const { container } = render(DashboardPage);
    await screen.findByText('One Piece');
    expect(container.querySelector('.series-grid.grid-large')).toBeTruthy();
    expect(container.querySelector('.series-table')).toBeNull();
  });

  it('default: vista persistita al mount', async () => {
    render(DashboardPage);
    await screen.findByText('One Piece');
    expect(localStorage.getItem(VIEW_KEY)).toBe('large');
  });

  it('legge la vista da localStorage (tabella)', async () => {
    localStorage.setItem(VIEW_KEY, 'table');
    const { container } = render(DashboardPage);
    await screen.findByText('Naruto');
    expect(container.querySelector('.series-table')).toBeTruthy();
    expect(container.querySelector('.series-grid')).toBeNull();
  });

  it('valore storage non valido: fallback a large', async () => {
    localStorage.setItem(VIEW_KEY, 'masonry');
    const { container } = render(DashboardPage);
    await screen.findByText('One Piece');
    expect(container.querySelector('.series-grid.grid-large')).toBeTruthy();
  });

  it('cambio vista: aggiorna la UI e persiste', async () => {    const { container } = render(DashboardPage);
    await screen.findByText('One Piece');
    await fireEvent.click(screen.getByTitle('Vista tabella'));
    expect(container.querySelector('.series-table')).toBeTruthy();
    expect(localStorage.getItem(VIEW_KEY)).toBe('table');
    await fireEvent.click(screen.getByTitle('Vista griglia'));
    expect(container.querySelector('.series-grid:not(.grid-large)')).toBeTruthy();
    expect(localStorage.getItem(VIEW_KEY)).toBe('grid');
  });

  it('bottone large: hook CSS per nasconderlo su telefono', async () => {
    // Il nascondiglio e media-query (non verificabile in happy-dom):
    // qui si fissa solo l'hook che il CSS usa.
    render(DashboardPage);
    await screen.findByText('One Piece');
    expect(screen.getByTitle('Vista grande').classList.contains('view-btn-large')).toBe(true);
  });
});

describe('DashboardPage ordinamento', () => {
  it('default: data inserimento desc (piu recenti prima), senza sort server', async () => {
    const { container } = render(DashboardPage);
    await screen.findByText('One Piece');
    // 'added' = ordine file senza parametri server...
    expect(listMock).toHaveBeenCalledTimes(1);
    expect(listMock).toHaveBeenCalledWith({});
    // ...invertito lato client (desc): Naruto (indice 1) prima di One Piece.
    const gridText = container.querySelector('.series-grid').textContent;
    expect(gridText.indexOf('Naruto')).toBeLessThan(gridText.indexOf('One Piece'));
  });

  it('legge l ordinamento da localStorage (nome asc)', async () => {
    localStorage.setItem(SORT_KEY, JSON.stringify({ field: 'name', dir: 'asc' }));
    const { container } = render(DashboardPage);
    await screen.findByText('One Piece');
    expect(listMock).toHaveBeenCalledWith({ sort: 'name', dir: 'asc' });
    const gridText = container.querySelector('.series-grid').textContent;
    expect(gridText.indexOf('One Piece')).toBeLessThan(gridText.indexOf('Naruto'));
  });

  it('storage non valido: fallback a data-desc', async () => {
    localStorage.setItem(SORT_KEY, '{rotto');
    render(DashboardPage);
    await screen.findByText('One Piece');
    expect(listMock).toHaveBeenCalledWith({});
  });

  it('toggle direzione: ricarica e persiste', async () => {
    const { container } = render(DashboardPage);
    await screen.findByText('One Piece');
    // Default desc -> click -> asc: niente reverse, torna ordine file.
    await fireEvent.click(screen.getByTitle('Decrescente'));
    expect(JSON.parse(localStorage.getItem(SORT_KEY))).toEqual({ field: 'added', dir: 'asc' });
    // Il reload e ri-render sono asincroni: attendi il secondo load
    // e poi il nuovo ordine nel DOM.
    await vi.waitFor(() => expect(listMock).toHaveBeenCalledTimes(2));
    await vi.waitFor(() => {
      const gridText = container.querySelector('.series-grid').textContent;
      expect(gridText.indexOf('One Piece')).toBeLessThan(gridText.indexOf('Naruto'));
    });
  });
});

describe('DashboardPage modali', () => {
  it('apertura dettaglio: lock scroll sfondo; chiusura: ripristino', async () => {
    render(DashboardPage);
    await screen.findByText('One Piece');
    expect(document.body.style.overflow).toBe('');
    // Le card hanno role=button con aria-label = nome serie.
    await fireEvent.click(screen.getByRole('button', { name: 'One Piece' }));
    await screen.findByTitle('Torna alle serie');
    expect(document.body.style.overflow).toBe('hidden');
    await fireEvent.click(screen.getByTitle('Torna alle serie'));
    await vi.waitFor(() => expect(document.body.style.overflow).toBe(''));
  });
});

describe('DashboardPage origine form', () => {
  it('dal dettaglio: back contestuale col nome, torna al dettaglio', async () => {
    const { container } = render(DashboardPage);
    await screen.findByText('One Piece');
    await fireEvent.click(screen.getByRole('button', { name: 'One Piece' }));
    await screen.findByTitle('Torna alle serie');
    // Il dettaglio e l'unico dialog: click su Modifica dentro di esso.
    await fireEvent.click(within(screen.getByRole('dialog')).getByText('Modifica'));
    await screen.findByText('Modifica: One Piece');
    // Back etichettato con l'origine, non generico.
    await fireEvent.click(screen.getByTitle('Torna a One Piece'));
    // Il form chiude e il dettaglio riappare (attesa fine transizioni).
    await vi.waitFor(() => expect(screen.queryByText('Modifica: One Piece')).toBeNull());
    await screen.findByTitle('Torna alle serie');
    expect(container.querySelector('.detail-modal')).toBeTruthy();
  });

  it('dal dettaglio: salva e torna al dettaglio aggiornato', async () => {
    updateMock.mockResolvedValue({ success: true });
    const callsBefore = listMock.mock.calls.length;
    render(DashboardPage);
    await screen.findByText('One Piece');
    await fireEvent.click(screen.getByRole('button', { name: 'One Piece' }));
    await screen.findByTitle('Torna alle serie');
    await fireEvent.click(within(screen.getByRole('dialog')).getByText('Modifica'));
    await screen.findByText('Modifica: One Piece');
    await fireEvent.click(screen.getByText('Salva Modifiche'));
    expect(updateMock).toHaveBeenCalledWith(0, expect.objectContaining({ name: 'One Piece' }));
    // Reload + riapertura dettaglio.
    await vi.waitFor(() => expect(listMock.mock.calls.length).toBeGreaterThan(callsBefore));
    await screen.findByTitle('Torna alle serie');
    expect(screen.queryByText('Modifica: One Piece')).toBeNull();
  });

  it('dalla lista (FAB): back torna alla lista, niente dettaglio', async () => {
    const { container } = render(DashboardPage);
    await screen.findByText('One Piece');
    await fireEvent.click(screen.getByLabelText('Aggiungi Serie'));
    await screen.findByText('Aggiungi Nuova Serie');
    expect(screen.getByTitle('Torna alle serie')).toBeTruthy();
    await fireEvent.click(screen.getByTitle('Torna alle serie'));
    await vi.waitFor(() => expect(screen.queryByText('Aggiungi Nuova Serie')).toBeNull());
    expect(container.querySelector('.detail-modal')).toBeNull();
    expect(container.querySelector('.series-grid')).toBeTruthy();
  });
});
