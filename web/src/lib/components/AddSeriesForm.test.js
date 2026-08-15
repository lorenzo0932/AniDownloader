import { describe, it, expect, vi } from 'vitest';
import { render, screen, fireEvent, waitFor } from '@testing-library/svelte';
import AddSeriesForm from './AddSeriesForm.svelte';
import { api } from '../api.js';

function formFixture(overrides = {}) {
  return {
    name: '',
    service: 'animeW_scraper',
    path: '',
    url: '',
    continue: false,
    highPriority: false,
    passedEpisodes: 0,
    ...overrides,
  };
}

const baseProps = {
  form: formFixture(),
  series: [],
  editing: -1,
};

describe('AddSeriesForm', () => {
  it('show=false: non renderizza nulla', () => {
    render(AddSeriesForm, { ...baseProps, show: false });
    expect(screen.queryByRole('dialog')).toBeNull();
    expect(screen.queryByText('Aggiungi Nuova Serie')).toBeNull();
  });

  it('show=true: renderizza il dialogo in modalità aggiunta', () => {
    render(AddSeriesForm, { ...baseProps, show: true });
    expect(screen.getByRole('dialog')).toBeTruthy();
    expect(screen.getByText('Aggiungi Nuova Serie')).toBeTruthy();
  });

  it('show=true editing>=0: titolo modifica + pulsante Elimina', () => {
    render(AddSeriesForm, {
      ...baseProps,
      show: true,
      editing: 0,
      form: formFixture({ name: 'Existing' }),
    });
    expect(screen.getByText('Modifica: Existing')).toBeTruthy();
    expect(screen.getByRole('button', { name: /Elimina Serie/ })).toBeTruthy();
  });

  it('click Annulla: invoca onclose', () => {
    const onclose = vi.fn();
    render(AddSeriesForm, { ...baseProps, show: true, onclose });
    fireEvent.click(screen.getByRole('button', { name: 'Annulla' }));
    expect(onclose).toHaveBeenCalledTimes(1);
  });

  it('click Salva Modifiche (edit): invoca onsave', () => {
    const onsave = vi.fn();
    render(AddSeriesForm, {
      ...baseProps,
      show: true,
      editing: 0,
      form: formFixture({ name: 'Existing' }),
      onsave,
    });
    fireEvent.click(screen.getByRole('button', { name: 'Salva Modifiche' }));
    expect(onsave).toHaveBeenCalledTimes(1);
  });

  it('click Salva (aggiunta): invoca onsave', () => {
    const onsave = vi.fn();
    render(AddSeriesForm, { ...baseProps, show: true, onsave });
    fireEvent.click(screen.getByRole('button', { name: 'Salva Modifiche' }));
    expect(onsave).toHaveBeenCalledTimes(1);
  });

  it('click Elimina Serie (edit): invoca ondelete', () => {
    const ondelete = vi.fn();
    render(AddSeriesForm, {
      ...baseProps,
      show: true,
      editing: 0,
      form: formFixture({ name: 'Existing' }),
      ondelete,
    });
    fireEvent.click(screen.getByRole('button', { name: /Elimina Serie/ }));
    expect(ondelete).toHaveBeenCalledTimes(1);
  });

  it('fetchName ok: chiama api.series.fetchName e popola form.name', async () => {
    const fetchNameSpy = vi.spyOn(api.series, 'fetchName').mockResolvedValue({ name: 'NomeRecuperato' });
    const form = formFixture({ url: 'https://example.com/series' });
    render(AddSeriesForm, { ...baseProps, show: true, form });
    fireEvent.click(screen.getByRole('button', { name: 'Recupera il nome dalla pagina web' }));
    await waitFor(() => {
      expect(fetchNameSpy).toHaveBeenCalledWith('https://example.com/series');
    });
    await waitFor(() => {
      expect(form.name).toBe('NomeRecuperato');
    });
  });

  it('fetchName ko: chiama onerror con messaggio', async () => {
    const fetchNameSpy = vi.spyOn(api.series, 'fetchName').mockRejectedValue(new Error('network down'));
    const onerror = vi.fn();
    const form = formFixture({ url: 'https://example.com/series' });
    render(AddSeriesForm, { ...baseProps, show: true, form, onerror });
    fireEvent.click(screen.getByRole('button', { name: 'Recupera il nome dalla pagina web' }));
    await waitFor(() => {
      expect(fetchNameSpy).toHaveBeenCalled();
    });
    await waitFor(() => {
      expect(onerror).toHaveBeenCalledWith('Impossibile recuperare il nome: network down');
    });
  });

  it('fetchName con url vuoto: non chiama l\'API', () => {
    const fetchNameSpy = vi.spyOn(api.series, 'fetchName');
    const form = formFixture({ url: '  ' });
    render(AddSeriesForm, { ...baseProps, show: true, form });
    fireEvent.click(screen.getByRole('button', { name: 'Recupera il nome dalla pagina web' }));
    expect(fetchNameSpy).not.toHaveBeenCalled();
  });
});
