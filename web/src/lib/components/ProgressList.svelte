<script>
  import { tick } from 'svelte';
  import TaskItem from './TaskItem.svelte';
  import { taskGroup } from '../downloadState.js';
  import { captureTaskRects, moveTasks } from '../taskMotion.js';

  let { tasks = [], waitingSeries = [], hydration = 0 } = $props();
  let board;
  let expanded = $state({ completed: false, skipped: false, pending: false });
  let showAllWaiting = $state(false);
  const groups = [
    { id: 'conversion', title: 'In conversione', icon: '⚙', empty: 'Nessuna conversione in corso' },
    { id: 'download', title: 'In download', icon: '↓', empty: 'Nessun download in corso' },
    { id: 'waiting', title: 'Scaricati, in attesa di conversione', icon: '◷', empty: 'Nessun episodio in attesa' },
    { id: 'errors', title: 'Errori e interruzioni', icon: '!', optional: true },
    { id: 'completed', title: 'Completati', icon: '✓', collapsed: true },
    { id: 'skipped', title: 'Saltati', icon: '–', collapsed: true },
    { id: 'pending', title: 'Da elaborare', icon: '…', collapsed: true },
  ];
  let grouped = $derived.by(() => {
    const value = Object.fromEntries(groups.map(g => [g.id, []]));
    for (const task of tasks) value[taskGroup(task)].push(task);
    for (const series of waitingSeries) value.pending.push({ key: `series:${series.name}`,
      seriesName: series.name, episode: 0, phase: 'queued', percent: -1, detail: 'Da analizzare' });
    for (const list of Object.values(value)) list.sort((a, b) =>
      a.seriesName.localeCompare(b.seriesName, 'it') || a.episode - b.episode);
    return value;
  });

  let lastHydration = -1;
  let lastLayout = '';
  let generation = 0;
  $effect.pre(() => {
    const layout = tasks.map(t => `${t.key}:${t.phase}`).sort().join('|') +
      JSON.stringify(expanded) + showAllWaiting;
    const hydrate = hydration;
    if (layout === lastLayout && hydrate === lastHydration) return;
    const previous = captureTaskRects(board);
    const animate = hydrate === lastHydration;
    lastHydration = hydrate;
    lastLayout = layout;
    const version = ++generation;
    tick().then(() => { if (animate && version === generation) moveTasks(board, previous); });
  });

  function goTo(id) {
    const section = board?.querySelector(`#home-${id}`);
    section?.scrollIntoView({ block: 'start', behavior: 'auto' });
    section?.focus({ preventScroll: true });
  }
</script>

<nav class="phase-overview" aria-label="Riepilogo elaborazione">
  {#each groups.filter(g => ['conversion', 'download', 'waiting', 'completed'].includes(g.id)) as group}
    <button onclick={() => goTo(group.id)} aria-label={`${group.title}: ${grouped[group.id].length}`}>
      <span aria-hidden="true">{group.icon}</span>
      <strong>{grouped[group.id].length}</strong>
      <span>{group.id === 'waiting' ? 'In attesa' : group.title}</span>
    </button>
  {/each}
</nav>

<div class="phase-board" bind:this={board}>
  {#each groups as group (group.id)}
    {#if !group.optional || grouped[group.id].length}
      <section class="phase-panel panel-{group.id}" id="home-{group.id}" tabindex="-1" aria-labelledby={`title-${group.id}`}>
        {#if group.collapsed}
          <h2 id={`title-${group.id}`}>
            <button class="section-toggle" aria-expanded={expanded[group.id]}
              aria-controls={`items-${group.id}`} onclick={() => expanded[group.id] = !expanded[group.id]}>
              <span>{group.title}</span><span class="count">{grouped[group.id].length}</span>
              <span aria-hidden="true">{expanded[group.id] ? '▾' : '▸'}</span>
            </button>
          </h2>
        {:else}
          <h2 id={`title-${group.id}`}><span aria-hidden="true">{group.icon}</span> {group.title}
            <span class="count">{grouped[group.id].length}</span></h2>
        {/if}
        <div id={`items-${group.id}`} hidden={group.collapsed && !expanded[group.id]} class="phase-items">
          {#if !group.collapsed || expanded[group.id]}
            {#each (group.id === 'waiting' && !showAllWaiting ? grouped[group.id].slice(0, 3) : grouped[group.id]) as task (task.key)}
              <div data-task-key={task.key}><TaskItem variant="phase" {task} /></div>
            {:else}
              <p class="phase-empty">{group.empty || 'Nessun episodio'}</p>
            {/each}
            {#if group.id === 'waiting' && grouped.waiting.length > 3}
              <button class="show-all" aria-expanded={showAllWaiting} onclick={() => showAllWaiting = !showAllWaiting}>
                {showAllWaiting ? 'Mostra meno' : `Mostra tutti (${grouped.waiting.length})`}
              </button>
            {/if}
          {/if}
        </div>
      </section>
    {/if}
  {/each}
</div>

<style>
  .phase-overview { display: grid; grid-template-columns: repeat(4, minmax(0, 1fr)); gap: 0.5rem; }
  .phase-overview button { display: flex; align-items: center; justify-content: center; gap: 0.45rem; min-width: 0; min-height: 44px; padding: 0.6rem; background: var(--bg-secondary); color: var(--text-secondary); border: 1px solid var(--border-color); border-radius: 8px; cursor: pointer; font: inherit; font-size: 0.8rem; }
  .phase-overview strong { color: var(--text-primary); font-variant-numeric: tabular-nums; }
  .phase-board { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); align-items: start; gap: 1rem; min-width: 0; overflow-anchor: none; }
  .phase-panel { min-width: 0; padding: 1rem; border: 1px solid var(--border-color); border-radius: 12px; background: var(--bg-primary); scroll-margin-top: 1rem; }
  .phase-panel:focus-visible { outline: 2px solid var(--accent); outline-offset: 3px; }
  .panel-conversion { border-top: 3px solid var(--warning); }
  .panel-download { border-top: 3px solid var(--accent); }
  .panel-waiting { border-top: 3px solid var(--success); }
  .panel-errors { border-color: var(--danger); }
  .phase-panel:not(.panel-conversion):not(.panel-download) { grid-column: 1 / -1; }
  h2 { display: flex; align-items: center; gap: 0.5rem; font-size: 0.86rem; font-weight: 600; color: var(--text-primary); margin: 0; }
  .count { margin-left: auto; background: var(--bg-tertiary); color: var(--text-secondary); border-radius: 1rem; padding: 0.15rem 0.5rem; font-size: 0.76rem; font-variant-numeric: tabular-nums; }
  .phase-items { display: flex; flex-direction: column; gap: 0.5rem; margin-top: 0.75rem; }
  .phase-items[hidden] { display: none; }
  [data-task-key] { position: relative; min-width: 0; }
  .phase-empty { margin: 0; padding: 0.6rem 0; color: var(--text-muted); font-size: 0.8rem; }
  .section-toggle { width: 100%; display: flex; align-items: center; gap: 0.65rem; min-height: 44px; padding: 0; border: 0; background: none; color: inherit; font: inherit; text-align: left; cursor: pointer; }
  .show-all { align-self: flex-start; min-height: 44px; padding: 0.35rem 0.7rem; border: 1px solid var(--border-color); border-radius: 6px; background: var(--bg-secondary); color: var(--text-secondary); cursor: pointer; }
  @media (max-width: 768px) {
    .phase-board { grid-template-columns: minmax(0, 1fr); gap: 0.75rem; }
    .phase-overview { grid-template-columns: repeat(2, minmax(0, 1fr)); }
    .phase-overview button { justify-content: flex-start; }
    .phase-panel { padding: 0.75rem; }
  }
</style>
