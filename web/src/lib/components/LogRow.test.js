import { describe, it, expect } from 'vitest';
import { render, screen } from '@testing-library/svelte';
import LogRow from './LogRow.svelte';

function voce(over = {}) {
  return {
    id: '1',
    time: '2026-11-08 18:33:05',
    level: 'INFO',
    message: 'Download execution completed',
    raw: '[2026-11-08 18:33:05] [INFO  ] Download execution completed',
    ...over,
  };
}

describe('LogRow', () => {
  it('mostra timestamp abbreviato, badge livello e messaggio', () => {
    const { container } = render(LogRow, { entry: voce() });
    expect(screen.getByText('18:33:05')).toBeTruthy();
    expect(screen.getByText('INFO')).toBeTruthy();
    expect(screen.getByText('Download execution completed')).toBeTruthy();
    expect(container.querySelector('.log-row').getAttribute('title')).toContain(
      '[2026-11-08 18:33:05]'
    );
  });

  it('timestamp: title con il valore completo', () => {
    render(LogRow, { entry: voce() });
    expect(screen.getByText('18:33:05').getAttribute('title')).toBe('2026-11-08 18:33:05');
  });

  it.each([
    ['ERROR', 'livello-error'],
    ['WARN', 'livello-warn'],
    ['INFO', 'livello-info'],
    ['RESULT', 'livello-result'],
  ])('badge %s con classe %s', (level, classe) => {
    const { container } = render(LogRow, { entry: voce({ level }) });
    const badge = container.querySelector('.log-level');
    expect(badge.textContent.trim()).toBe(level);
    expect(badge.classList.contains(classe)).toBe(true);
  });

  it('fallback ALTRO per una riga non strutturata', () => {
    const { container } = render(LogRow, {
      entry: voce({
        time: '',
        level: 'ALTRO',
        message: 'riga senza formato',
        raw: 'riga senza formato',
      }),
    });
    const badge = container.querySelector('.log-level');
    expect(badge.textContent.trim()).toBe('ALTRO');
    expect(badge.classList.contains('livello-altro')).toBe(true);
    expect(screen.getByText('riga senza formato')).toBeTruthy();
  });

  it('evidenzia tutte le occorrenze della query senza distinzione di maiuscole', () => {
    const { container } = render(LogRow, {
      entry: voce({ message: 'Download fallito: download interrotto' }),
      query: 'DOWNLOAD',
    });
    const mark = [...container.querySelectorAll('mark')];
    expect(mark).toHaveLength(2);
    expect(mark[0].textContent).toBe('Download');
    expect(mark[1].textContent).toBe('download');
  });

  it('query assente: nessun mark', () => {
    const { container } = render(LogRow, { entry: voce() });
    expect(container.querySelectorAll('mark')).toHaveLength(0);
  });

  it('wrap disattivo di default e attivo con la prop', async () => {
    const { container, rerender } = render(LogRow, { entry: voce() });
    expect(container.querySelector('.log-row').classList.contains('wrap')).toBe(false);
    await rerender({ entry: voce(), wrap: true });
    expect(container.querySelector('.log-row').classList.contains('wrap')).toBe(true);
  });
});
