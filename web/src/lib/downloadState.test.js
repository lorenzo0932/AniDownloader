import { describe, it, expect } from 'vitest';
import { emptyDownloadState, applyDownloadEvent, taskGroup, eventDescription } from './downloadState.js';

const task = (phase, percent = -1) => ({ key: '["A",1]', seriesName: 'A', episode: 1, phase, percent });
const event = (sequence, phase, percent = -1) => ({ sequence, runId: 1, type: 'progress', task: task(phase, percent) });

describe('Stato download Home', () => {
  it('download 99 -> attesa senza percentuale -> conversione indipendente', () => {
    let state = applyDownloadEvent(emptyDownloadState(), event(1, 'downloading', 99));
    expect(taskGroup(state.tasks[0])).toBe('download');
    state = applyDownloadEvent(state, event(2, 'waiting_conversion'));
    expect(state.tasks).toHaveLength(1);
    expect(taskGroup(state.tasks[0])).toBe('waiting');
    expect(state.tasks[0].percent).toBe(-1);
    state = applyDownloadEvent(state, event(3, 'converting', 8));
    expect(state.tasks[0].percent).toBe(8);
    expect(taskGroup(state.tasks[0])).toBe('conversion');
  });
  it('snapshot + evento vecchio/duplicato non fanno regredire lo stato', () => {
    const snapshot = { ...emptyDownloadState(), runId: 1, sequence: 8, tasks: [task('converting', 20)] };
    expect(applyDownloadEvent(snapshot, event(7, 'downloading', 99))).toBe(snapshot);
    expect(applyDownloadEvent(snapshot, event(8, 'downloading', 99))).toBe(snapshot);
  });
  it('nuovo run azzera i task; vecchio run ignorato', () => {
    const old = applyDownloadEvent(emptyDownloadState(), event(1, 'completed', 100));
    const state = applyDownloadEvent(old, { type: 'started', runId: 2, sequence: 2 });
    expect(state.tasks).toEqual([]);
    expect(state.running).toBe(true);
    expect(applyDownloadEvent(state, event(3, 'converting', 1))).toBe(state);
  });
  it('stop non marca come completati gli episodi in attesa', () => {
    const old = applyDownloadEvent(emptyDownloadState(), event(1, 'waiting_conversion'));
    const state = applyDownloadEvent(old, { type: 'done', runId: 1, sequence: 2, stopped: true });
    expect(state.tasks[0].phase).toBe('interrupted');
    expect(state.tasks[0].percent).toBe(-1);
    expect(taskGroup(state.tasks[0])).toBe('errors');
  });
  it('aggiornamenti percentuali senza log non aggiungono eventi', () => {
    let state = applyDownloadEvent(emptyDownloadState(), { ...event(1, 'downloading', 1), log: { id: 1 } });
    state = applyDownloadEvent(state, event(2, 'downloading', 42));
    expect(state.events).toHaveLength(1);
  });
  it('episodio sostituisce l’analisi della serie senza duplicarla', () => {
    const old = { ...emptyDownloadState(), runId: 1, tasks: [{ ...task('analysing'), key: 'analysis', episode: 0 }] };
    expect(applyDownloadEvent(old, event(1, 'downloading', 0)).tasks).toHaveLength(1);
  });
  it('log leggibile senza dump tecnico', () => {
    expect(eventDescription({ type: 'progress', task: task('waiting_conversion') })).toContain('in attesa');
    expect(eventDescription({ type: 'done', stopped: true })).toContain('interrotta');
  });
});
