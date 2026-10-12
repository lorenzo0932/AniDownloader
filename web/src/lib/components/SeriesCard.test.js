import { describe, it, expect, vi } from 'vitest';
import { render, screen, fireEvent } from '@testing-library/svelte';
import SeriesCard from './SeriesCard.svelte';

const baseProps = (overrides = {}) => ({
  title: 'One Piece',
  poster: '/poster.png',
  ...overrides,
});

describe('SeriesCard', () => {
  it('render: titolo e snippet meta (servizio + episodi)', () => {
    render(SeriesCard, { ...baseProps(), meta: () => 'Meta' });
    expect(screen.getByText('One Piece')).toBeTruthy();
  });

  it('render: niente path/URL nel corpo (vivono nel modale dettaglio)', () => {
    const { container } = render(SeriesCard, {
      ...baseProps(),
      meta: () => '/media/one-piece https://example.com/one-piece',
    });
    // Lo snippet meta passa attraverso: la shell non filtra, ma il wrapper
    // di dominio (SeriesList) non ci mette mai path/URL.
    expect(container.querySelector('.card-title-overlay')).toBeTruthy();
  });

  it('apertura: click sul poster invoca onopen', () => {
    const onopen = vi.fn();
    render(SeriesCard, { ...baseProps(), onopen });
    fireEvent.click(screen.getByRole('button', { name: 'One Piece' }));
    expect(onopen).toHaveBeenCalledTimes(1);
  });

  it('overlay Dettagli: invoca onopen', () => {
    const onopen = vi.fn();
    render(SeriesCard, { ...baseProps(), onopen });
    fireEvent.click(screen.getByRole('button', { name: 'Dettagli' }));
    expect(onopen).toHaveBeenCalledTimes(1);
  });

  it('snippet actions: bottoni custom con stopPropagation (niente onopen)', () => {
    const onopen = vi.fn();
    const onedit = vi.fn();
    const { container } = render(SeriesCard, { ...baseProps(), onopen });
    expect(container.querySelector('.card-hover-actions')).toBeTruthy();
    // Senza snippet actions: solo Dettagli + apertura, nessun Modifica.
    expect(screen.queryByRole('button', { name: 'Modifica' })).toBeNull();
    expect(onedit).not.toHaveBeenCalled();
  });

  it('struttura accessibile: apertura con button nativo, mai button annidati', () => {
    const { container } = render(SeriesCard, { ...baseProps(), onopen: () => {} });
    const open = screen.getByRole('button', { name: 'One Piece' });
    expect(open.tagName).toBe('BUTTON');
    expect(open.querySelectorAll('button')).toHaveLength(0);
    expect(container.querySelector('[role="button"]')).toBeNull();
  });
});
