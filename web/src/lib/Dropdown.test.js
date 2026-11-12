import { describe, it, expect, vi } from 'vitest';
import { render, screen, fireEvent } from '@testing-library/svelte';
import Dropdown from './Dropdown.svelte';

const OPTIONS = [
  { value: 'a', label: 'Alfa' },
  { value: 'b', label: 'Beta' },
  { value: 'c', label: 'Gamma' },
];

describe('Dropdown', () => {
  it('apre la lista, seleziona col mouse e chiama onchange', async () => {
    const onchange = vi.fn();
    render(Dropdown, { value: 'a', options: OPTIONS, onchange, label: 'Filtro', id: 'f' });
    const combo = screen.getByRole('combobox', { name: 'Filtro' });
    expect(combo.textContent).toContain('Alfa');
    await fireEvent.click(combo);
    expect(screen.getByRole('listbox', { name: 'Filtro' })).toBeTruthy();
    await fireEvent.click(screen.getByRole('option', { name: 'Beta' }));
    expect(onchange).toHaveBeenCalledWith('b');
    expect(screen.queryByRole('listbox')).toBeNull();
  });

  it('supporta frecce, Home/End ed Enter senza mouse', async () => {
    const onchange = vi.fn();
    render(Dropdown, { value: 'a', options: OPTIONS, onchange, label: 'Filtro', id: 'k' });
    const combo = screen.getByRole('combobox', { name: 'Filtro' });
    combo.focus();
    await fireEvent.keyDown(combo, { key: 'ArrowDown' });
    await fireEvent.keyDown(combo, { key: 'ArrowDown' });
    await fireEvent.keyDown(combo, { key: 'Enter' });
    expect(onchange).toHaveBeenCalledWith('b');

    await fireEvent.keyDown(combo, { key: 'ArrowDown' });
    await fireEvent.keyDown(combo, { key: 'End' });
    await fireEvent.keyDown(combo, { key: 'Enter' });
    expect(onchange).toHaveBeenLastCalledWith('c');
  });

  it('Escape chiude senza selezionare', async () => {
    const onchange = vi.fn();
    render(Dropdown, { value: 'a', options: OPTIONS, onchange, label: 'Filtro', id: 'e' });
    const combo = screen.getByRole('combobox', { name: 'Filtro' });
    await fireEvent.click(combo);
    expect(screen.getByRole('listbox')).toBeTruthy();
    await fireEvent.keyDown(combo, { key: 'Escape' });
    expect(screen.queryByRole('listbox')).toBeNull();
    expect(onchange).not.toHaveBeenCalled();
  });

  it('disabled: non apre e non chiama onchange', async () => {
    const onchange = vi.fn();
    render(Dropdown, {
      value: 'a', options: OPTIONS, onchange, label: 'Filtro', id: 'd', disabled: true,
    });
    const combo = screen.getByRole('combobox', { name: 'Filtro' });
    expect(combo.disabled).toBe(true);
    await fireEvent.click(combo);
    expect(screen.queryByRole('listbox')).toBeNull();
    expect(onchange).not.toHaveBeenCalled();
  });
});
