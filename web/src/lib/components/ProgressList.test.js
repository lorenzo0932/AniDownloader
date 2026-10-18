import { describe, it, expect } from 'vitest';
import { render, screen, fireEvent } from '@testing-library/svelte';
import ProgressList from './ProgressList.svelte';

const task = (name, phase, percent = -1) => ({ key: name, seriesName: name, episode: 2, phase, percent, detail: 'Dettaglio' });

describe('Pannelli Home', () => {
  it('divide le fasi in pannelli reali, senza barre nelle attese', () => {
    const { container } = render(ProgressList, { tasks: [task('DL', 'downloading', 99), task('WAIT', 'waiting_conversion'), task('CONV', 'converting', 12)] });
    expect(container.querySelector('#home-download').textContent).toContain('DL');
    expect(container.querySelector('#home-conversion').textContent).toContain('CONV');
    expect(container.querySelector('#home-waiting').textContent).toContain('WAIT');
    expect(container.querySelector('#home-waiting progress')).toBeNull();
    expect(container.querySelectorAll('progress')).toHaveLength(2);
  });
  it('saltati e completati chiusi, errori subito visibili', async () => {
    render(ProgressList, { tasks: [task('Saltata', 'skipped'), task('Pronta', 'completed'), task('Fallita', 'error')] });
    expect(screen.queryByText('Saltata')).toBeNull();
    expect(screen.queryByText('Pronta')).toBeNull();
    expect(screen.getByText('Fallita')).toBeTruthy();
    const skip = screen.getByRole('button', { name: /Saltati/ });
    expect(skip.getAttribute('aria-expanded')).toBe('false');
    await fireEvent.click(skip);
    expect(screen.getByText('Saltata')).toBeTruthy();
  });
  it('lo stesso episodio passa da download ad attesa senza duplicati', async () => {
    const { container, rerender } = render(ProgressList, { tasks: [task('Serie', 'downloading', 99)] });
    await rerender({ tasks: [task('Serie', 'waiting_conversion')] });
    expect(screen.getAllByText('Serie')).toHaveLength(1);
    expect(container.querySelector('#home-download progress')).toBeNull();
    expect(container.querySelector('#home-waiting').textContent).toContain('Serie');
  });
  it('attese lunghe compatte ma espandibili', async () => {
    render(ProgressList, { tasks: Array.from({ length: 5 }, (_, i) => task(`Serie${i}`, 'waiting_conversion')) });
    expect(screen.queryByText('Serie4')).toBeNull();
    await fireEvent.click(screen.getByRole('button', { name: 'Mostra tutti (5)' }));
    expect(screen.getByText('Serie4')).toBeTruthy();
  });
  it('usa le miniature delle serie senza trasformare le righe in pulsanti', () => {
    const { container } = render(ProgressList, { tasks: [task('Serie', 'converting', 12)],
      seriesList: [{ name: 'Serie', path: '/media/serie' }] });
    expect(container.querySelector('.task-poster').getAttribute('src')).toContain('path=%2Fmedia%2Fserie');
    expect(container.querySelector('.task-card button')).toBeNull();
    expect(container.querySelector('.phase-overview')).toBeNull();
  });
  it('a riposo mostra solo i risultati presenti e non pannelli attivi vuoti', () => {
    const { container } = render(ProgressList, { running: false,
      tasks: [task('Pronta', 'completed'), task('Fallita', 'error')] });
    expect(container.querySelector('#home-download')).toBeNull();
    expect(container.querySelector('#home-conversion')).toBeNull();
    expect(container.querySelector('#home-waiting')).toBeNull();
    expect(screen.getByText('Fallita')).toBeTruthy();
    expect(screen.queryByText('Pronta')).toBeNull();
  });
});
