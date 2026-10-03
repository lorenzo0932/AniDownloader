import { describe, it, expect, vi } from 'vitest';
import { render, screen, fireEvent } from '@testing-library/svelte';
import DetailModal from './DetailModal.svelte';

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

const seriesFixture = () => ({
  _file_index: 0,
  name: 'One Piece',
  path: '/media/one-piece',
  service: 'animeW_scraper',
  continue: true,
  is_high_priority: true,
  local_episode_count: 100,
  passed_episodes: 0,
  series_page_url: 'https://example.com/one-piece',
});

const renderModal = (over = {}) => {
  const handlers = { onclose: vi.fn(), onedit: vi.fn(), onremove: vi.fn() };
  const props = {
    series: seriesFixture(),
    description: 'Pirati gommosi.',
    poster: '/poster480',
    srcset: '',
    fullPoster: '/poster1080',
    ...handlers,
    ...over,
  };
  const out = render(DetailModal, props);
  return { ...out, handlers };
};

describe('DetailModal', () => {
  it('render: titolo, meta, azioni', () => {
    const { container } = renderModal();
    expect(screen.getByText('One Piece')).toBeTruthy();
    expect(screen.getByText('Alta Priorità')).toBeTruthy();
    expect(screen.getByText('Modifica')).toBeTruthy();
    expect(screen.getByText('Elimina')).toBeTruthy();
    expect(container.querySelector('.detail-actions')).toBeTruthy();
  });

  it('back "Serie": chiude (torna alle serie)', async () => {
    const { handlers } = renderModal();
    await fireEvent.click(screen.getByTitle('Torna alle serie'));
    expect(handlers.onclose).toHaveBeenCalledTimes(1);
  });

  it('Escape senza lightbox: chiude', async () => {
    const { handlers, container } = renderModal();
    await fireEvent.keyDown(container.querySelector('.modal-overlay'), { key: 'Escape' });
    expect(handlers.onclose).toHaveBeenCalledTimes(1);
  });

  it('click locandina: apre lightbox con full-res', async () => {
    const { container } = renderModal();
    expect(container.querySelector('.lightbox')).toBeNull();
    await fireEvent.click(screen.getByLabelText('Ingrandisci locandina'));
    const box = container.querySelector('.lightbox');
    expect(box).toBeTruthy();
    expect(box.querySelector('img').getAttribute('src')).toBe('/poster1080');
  });

  it('lightbox: fallback al poster se fullPoster assente', async () => {
    const { container } = renderModal({ fullPoster: '' });
    await fireEvent.click(screen.getByLabelText('Ingrandisci locandina'));
    expect(container.querySelector('.lightbox img').getAttribute('src')).toBe('/poster480');
  });

  it('click lightbox: chiude solo la lightbox, non il modale', async () => {
    const { handlers, container } = renderModal();
    await fireEvent.click(screen.getByLabelText('Ingrandisci locandina'));
    expect(container.querySelector('.lightbox')).toBeTruthy();
    await fireEvent.click(screen.getByLabelText('Locandina a tutto schermo'));
    // L'uscita animata trattiene il nodo (inert) fino a fine outro.
    await vi.waitFor(() => expect(container.querySelector('.lightbox')).toBeNull());
    expect(handlers.onclose).not.toHaveBeenCalled();
    expect(screen.getByText('One Piece')).toBeTruthy();
  });

  it('Escape con lightbox aperta: chiude lightbox, non il modale', async () => {
    const { handlers, container } = renderModal();
    await fireEvent.click(screen.getByLabelText('Ingrandisci locandina'));
    await fireEvent.keyDown(container.querySelector('.modal-overlay'), { key: 'Escape' });
    await vi.waitFor(() => expect(container.querySelector('.lightbox')).toBeNull());
    expect(handlers.onclose).not.toHaveBeenCalled();
  });
});
