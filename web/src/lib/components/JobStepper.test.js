import { describe, it, expect } from 'vitest';
import { render } from '@testing-library/svelte';
import JobStepper from './JobStepper.svelte';

function statoDi(container, label) {
  return container.querySelector(`[data-step="${label}"]`)?.getAttribute('data-stato');
}

describe('JobStepper', () => {
  it('queued_download: Coda è lo step corrente', () => {
    const { container } = render(JobStepper, { fase: 'queued_download' });
    expect(statoDi(container, 'Coda')).toBe('current');
    expect(statoDi(container, 'Download')).toBe('pending');
  });

  it('retry_wait: resta sulla Coda', () => {
    const { container } = render(JobStepper, { fase: 'retry_wait' });
    expect(statoDi(container, 'Coda')).toBe('current');
  });

  it('downloading: Download è lo step corrente', () => {
    const { container } = render(JobStepper, { fase: 'downloading' });
    expect(statoDi(container, 'Download')).toBe('current');
    expect(statoDi(container, 'Verifica')).toBe('pending');
  });

  it('downloaded_waiting_conversion: primi tre done, conversione e fatto pending', () => {
    const { container } = render(JobStepper, { fase: 'downloaded_waiting_conversion' });
    expect(statoDi(container, 'Coda')).toBe('done');
    expect(statoDi(container, 'Download')).toBe('done');
    expect(statoDi(container, 'Verifica')).toBe('done');
    expect(statoDi(container, 'Conversione')).toBe('pending');
    expect(statoDi(container, 'Fatto')).toBe('pending');
  });

  it('converting: conversione corrente, fatto pending', () => {
    const { container } = render(JobStepper, { fase: 'converting' });
    expect(statoDi(container, 'Coda')).toBe('done');
    expect(statoDi(container, 'Download')).toBe('done');
    expect(statoDi(container, 'Verifica')).toBe('done');
    expect(statoDi(container, 'Conversione')).toBe('current');
    expect(statoDi(container, 'Fatto')).toBe('pending');
  });

  it('completed: tutti gli step done', () => {
    const { container } = render(JobStepper, { fase: 'completed' });
    for (const label of ['Coda', 'Download', 'Verifica', 'Conversione', 'Fatto']) {
      expect(statoDi(container, label)).toBe('done');
    }
  });

  it.each(['failed', 'interrupted'])('%s: nessuno stepper (non si inventa lo step)', (fase) => {
    const { container } = render(JobStepper, { fase });
    expect(container.querySelector('.stepper')).toBeNull();
  });

  it('la parte grafica è aria-hidden', () => {
    const { container } = render(JobStepper, { fase: 'downloading' });
    expect(container.querySelector('.stepper').getAttribute('aria-hidden')).toBe('true');
  });
});
