import { describe, it, expect } from 'vitest';
import { render, screen } from '@testing-library/svelte';
import StateChip from './StateChip.svelte';

const CASI = [
  ['queued_download', 'In coda', 'stato-queued_download'],
  ['downloading', 'In download', 'stato-downloading'],
  ['downloaded_waiting_conversion', 'In attesa conversione', 'stato-downloaded_waiting_conversion'],
  ['converting', 'In conversione', 'stato-converting'],
  ['completed', 'Completato', 'stato-completed'],
  ['failed', 'Non riuscito', 'stato-failed'],
  ['interrupted', 'Interrotto', 'stato-interrupted'],
];

describe('StateChip', () => {
  it.each(CASI)('fase %s: etichetta e colore %s', (fase, etichetta, classe) => {
    const { container } = render(StateChip, { fase });
    const chip = container.querySelector('.job-stato');
    expect(chip).not.toBeNull();
    expect(chip.textContent.trim()).toBe(etichetta);
    expect(chip.classList.contains(classe)).toBe(true);
    expect(chip.getAttribute('data-fase')).toBe(fase);
    expect(screen.getByText(etichetta)).toBeTruthy();
  });

  it('retry_wait mostra "riprovo alle HH:MM" con orario locale', () => {
    const sec = Math.floor(Date.now() / 1000) + 120;
    render(StateChip, { fase: 'retry_wait', prossimaEsecuzione: sec });
    const ora = new Date(sec * 1000).toLocaleTimeString('it-IT', {
      hour: '2-digit', minute: '2-digit',
    });
    expect(screen.getByText(`In attesa · riprovo alle ${ora}`)).toBeTruthy();
  });

  it('retry_wait senza timestamp non mostra mai "In attesa" da solo', () => {
    const { container } = render(StateChip, { fase: 'retry_wait' });
    expect(container.textContent.trim()).toBe('In attesa · riprovo a breve');
    expect(container.textContent.trim()).not.toBe('In attesa');
  });

  it('fase sconosciuta: fallback sul valore grezzo', () => {
    const { container } = render(StateChip, { fase: 'strano' });
    expect(container.textContent.trim()).toBe('strano');
  });
});
