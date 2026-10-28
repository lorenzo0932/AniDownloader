export const emptyDownloadState = () => ({
  runId: 0, sequence: 0, running: false, phase: 'idle', status: 'Pronto.',
  stopped: false, tasks: [], events: [],
});

export const phaseLabels = Object.freeze({
  analysing: 'Analisi della serie', queued: 'Da elaborare',
  downloading: 'Download avviato', verifying_download: 'Download concluso · verifica integrità',
  waiting_conversion: 'Scaricato · in attesa di conversione', converting: 'Conversione avviata',
  finalizing: 'Finalizzazione del video', completed: 'Completato · pronto',
  error: 'Errore', interrupted: 'Interrotto', skipped: 'Saltato',
});

export function applyDownloadEvent(state, event) {
  if (!Number.isSafeInteger(event.sequence) || event.sequence <= state.sequence) return state;
  if (event.runId < state.runId) return state;
  let next = event.type === 'started' || event.runId > state.runId
    ? emptyDownloadState() : state;
  next = { ...next, sequence: event.sequence, runId: event.runId };
  if (event.type === 'started') {
    next.running = true;
    next.phase = 'analysis';
    next.status = 'Analisi in corso';
  }
  if (event.type === 'overall') next.status = event.status;
  if (event.type === 'phase') next.phase = event.phase;
  if (event.task) {
    const task = event.task;
    next.tasks = next.tasks.filter(t => t.key !== task.key &&
      !(task.episode > 0 && t.seriesName === task.seriesName && t.episode === 0));
    next.tasks = [...next.tasks, task];
  }
  if (event.log) next.events = [...next.events, event.log].slice(-120);
  if (event.type === 'done') {
    next.running = false;
    next.stopped = !!event.stopped;
    next.phase = 'done';
    next.status = next.stopped ? 'Elaborazione interrotta' : 'Elaborazione terminata';
    next.tasks = next.tasks.map(t => ['completed', 'skipped', 'error', 'interrupted'].includes(t.phase)
      ? t : { ...t, phase: next.stopped ? 'interrupted' : 'error', percent: -1,
        detail: next.stopped ? 'Elaborazione interrotta' : 'Esito non disponibile' });
  }
  return next;
}

export function taskGroup(task) {
  if (['converting', 'finalizing'].includes(task.phase)) return 'conversion';
  if (['downloading', 'verifying_download'].includes(task.phase)) return 'download';
  if (task.phase === 'waiting_conversion') return 'waiting';
  if (task.phase === 'completed') return 'completed';
  if (['error', 'interrupted'].includes(task.phase)) return 'errors';
  if (task.phase === 'skipped') return 'skipped';
  return 'pending';
}

export function eventDescription(event) {
  if (event.type === 'started') return 'Elaborazione avviata';
  if (event.type === 'done') return event.stopped ? 'Elaborazione interrotta' : 'Elaborazione terminata';
  const phase = event.task?.phase;
  const label = phaseLabels[phase] || 'Aggiornamento';
  return ['error', 'interrupted', 'skipped'].includes(phase) && event.task?.detail
    ? `${label}: ${event.task.detail}` : label;
}
