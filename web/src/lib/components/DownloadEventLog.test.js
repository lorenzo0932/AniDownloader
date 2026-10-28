import { describe, it, expect } from 'vitest';
import { render, screen, fireEvent } from '@testing-library/svelte';
import { tick } from 'svelte';
import DownloadEventLog from './DownloadEventLog.svelte';

const event = id => ({ id, timestamp: 1792300000000 + id * 1000, type: 'progress', series: 'Serie', episode: 2,
  task: { phase: 'waiting_conversion' } });

describe('Cronologia Home', () => {
  it('chiusa di default, eventi con orario e fase leggibile', async () => {
    render(DownloadEventLog, { props: { events: [event(1)] } });
    expect(screen.queryByText('Serie · Episodio 2')).toBeNull();
    await fireEvent.click(screen.getByRole('button', { name: /Eventi/ }));
    expect(screen.getByText('Serie · Episodio 2')).toBeTruthy();
    expect(screen.getByText('Scaricato · in attesa di conversione')).toBeTruthy();
    expect(document.querySelector('time').getAttribute('datetime')).toContain('T');
  });
  it('se l’utente risale non trascina la lettura al fondo', async () => {
    const { rerender } = render(DownloadEventLog, { props: { events: [event(1)] } });
    await fireEvent.click(screen.getByRole('button', { name: /Eventi/ }));
    const view = screen.getByRole('region');
    Object.defineProperty(view, 'scrollHeight', { value: 1000 });
    Object.defineProperty(view, 'clientHeight', { value: 200 });
    view.scrollTop = 100;
    await fireEvent.scroll(view);
    await rerender({ events: [event(1), event(2)] });
    await tick();
    expect(view.scrollTop).toBe(100);
    await fireEvent.click(screen.getByRole('button', { name: 'Vai agli ultimi eventi' }));
    await tick();
    expect(view.scrollTop).toBe(1000);
  });
});
