<script>
  // Lista raggruppata dei task di download (analisi/attive/completate/saltate/
  // in attesa). I toggle di espansione sono stato locale UI (non condiviso).
  import TaskItem from './TaskItem.svelte';
  import { posterUrl } from '../api.js';

  let { analysisSeries = [], activeTasks = [], doneTasks = [], skippedTasks = [], waitingSeries = [] } = $props();
  let skipExpanded = $state(false);
  let waitingExpanded = $state(false);
</script>

<div class="series-list">
  {#if analysisSeries.length > 0}
    <div class="group-label">Analisi in corso ({analysisSeries.length})</div>
    {#each analysisSeries as s, idx (s.name)}
      <TaskItem variant="analysing" series={s} poster={posterUrl(s.path)} index={idx} />
    {/each}
  {/if}

  {#if activeTasks.length > 0}
    <div class="group-label">In elaborazione ({activeTasks.length})</div>
    {#each activeTasks as task, idx (task.key)}
      <TaskItem variant="active" {task} index={idx} />
    {/each}
  {/if}

  {#if doneTasks.length > 0}
    <div class="group-label">Completate ({doneTasks.length})</div>
    {#each doneTasks as task, idx (task.key)}
      <TaskItem variant="done" {task} index={idx} />
    {/each}
  {/if}

  {#if skippedTasks.length > 0}
    <div class="group-label">
      <button class="group-toggle" onclick={() => skipExpanded = !skipExpanded}>
        Saltate ({skippedTasks.length}) {skipExpanded ? '\u25BC' : '\u25B6'}
      </button>
    </div>
    {#if skipExpanded}
      {#each skippedTasks as task, idx (task.key)}
        <TaskItem variant="skipped" {task} index={idx} />
      {/each}
    {/if}
  {/if}

  {#if waitingSeries.length > 0}
    <div class="group-label">
      <button class="group-toggle" onclick={() => waitingExpanded = !waitingExpanded}>
        In attesa ({waitingSeries.length}) {waitingExpanded ? '\u25BC' : '\u25B6'}
      </button>
    </div>
    {#if waitingExpanded}
      {#each waitingSeries as s, idx (s.name)}
        <TaskItem variant="waiting" series={s} index={idx} />
      {/each}
    {/if}
  {/if}
</div>

<style>
  .series-list { display: flex; flex-direction: column; gap: 0.25rem; }
  .group-label {
    font-size: 0.8rem; font-weight: 600; color: var(--accent-light);
    text-transform: uppercase; letter-spacing: 0.05em;
    padding: 0.75rem 0 0.25rem; border-bottom: 1px solid var(--border-color); margin-bottom: 0.25rem;
  }
  .group-toggle {
    background: none; border: none; color: var(--accent-light); cursor: pointer;
    font-size: 0.8rem; font-weight: 600; text-transform: uppercase; letter-spacing: 0.05em;
    padding: 0;
  }
  .group-toggle:hover { color: var(--accent); }
</style>
