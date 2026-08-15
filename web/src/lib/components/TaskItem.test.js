import { describe, it, expect } from 'vitest';
import { render, screen } from '@testing-library/svelte';
import TaskItem from './TaskItem.svelte';

const seriesFixture = { name: 'My Series' };
const taskFixture = (overrides = {}) => ({
  seriesName: 'My Series',
  episode: 3,
  statusText: 'Downloading...',
  percent: 42.6,
  ...overrides,
});

describe('TaskItem', () => {
  it('variante analysing: nome serie + testo "Analisi..."', () => {
    render(TaskItem, { variant: 'analysing', series: seriesFixture });
    const row = document.querySelector('.series-row.analysing');
    expect(row).not.toBeNull();
    expect(screen.getByText('My Series')).toBeTruthy();
    expect(screen.getByText('Analisi...')).toBeTruthy();
  });

  it('variante active: nome con episodio + statusText + percentuale arrotondata', () => {
    render(TaskItem, { variant: 'active', task: taskFixture() });
    const row = document.querySelector('.series-row.active');
    expect(row).not.toBeNull();
    expect(screen.getByText('My Series (Ep 3)')).toBeTruthy();
    expect(screen.getByText('Downloading...')).toBeTruthy();
    const pbar = document.querySelector('.pbar-fill');
    expect(pbar.style.width).toBe('42.6%');
    expect(screen.getByText('43%')).toBeTruthy();
  });

  it('variante active senza episodio: solo nome', () => {
    render(TaskItem, { variant: 'active', task: taskFixture({ episode: 0 }) });
    expect(screen.getByText('My Series')).toBeTruthy();
    expect(screen.queryByText('My Series (Ep 0)')).toBeNull();
  });

  it('variante done: testo ok + classe status ok', () => {
    render(TaskItem, { variant: 'done', task: taskFixture({ statusText: 'Completata' }) });
    const row = document.querySelector('.series-row.done');
    expect(row).not.toBeNull();
    expect(screen.getByText('Completata')).toBeTruthy();
    expect(document.querySelector('.series-status.ok')).not.toBeNull();
  });

  it('variante skipped: testo skip + classe status skip', () => {
    render(TaskItem, { variant: 'skipped', task: taskFixture({ statusText: 'Saltata' }) });
    const row = document.querySelector('.series-row.skipped');
    expect(row).not.toBeNull();
    expect(screen.getByText('Saltata')).toBeTruthy();
    expect(document.querySelector('.series-status.skip')).not.toBeNull();
  });

  it('variante waiting: nome serie senza stato', () => {
    render(TaskItem, { variant: 'waiting', series: seriesFixture });
    const row = document.querySelector('.series-row.waiting');
    expect(row).not.toBeNull();
    expect(screen.getByText('My Series')).toBeTruthy();
    expect(screen.queryByText('Analisi...')).toBeNull();
    expect(screen.queryByText('Downloading...')).toBeNull();
  });
});
