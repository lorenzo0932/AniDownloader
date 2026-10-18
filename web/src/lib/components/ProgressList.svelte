<script>
  import { tick } from 'svelte';
  import TaskItem from './TaskItem.svelte';
  import { taskGroup } from '../downloadState.js';
  import { captureTaskRects, moveTasks } from '../taskMotion.js';
  import { posterUrl } from '../api.js';

  let { tasks = [], waitingSeries = [], seriesList = [], running = true, hydration = 0 } = $props();
  let paths = $derived(new Map(seriesList.map(s => [s.name, s.path])));
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

</script>

<div class="phase-board" bind:this={board}>
  {#each groups as group (group.id)}
    {#if (!group.optional && running) || grouped[group.id].length}
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
          <h2 id={`title-${group.id}`}>{group.title}
            <span class="count">{grouped[group.id].length}</span></h2>
        {/if}
        <div id={`items-${group.id}`} hidden={group.collapsed && !expanded[group.id]} class="phase-items">
          {#if !group.collapsed || expanded[group.id]}
            {#each (group.id === 'waiting' && !showAllWaiting ? grouped[group.id].slice(0, 3) : grouped[group.id]) as task (task.key)}
              <div data-task-key={task.key}><TaskItem variant="phase" {task} poster={posterUrl(paths.get(task.seriesName), 96)} /></div>
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
  .phase-board { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); align-items: start; gap: 1rem 1.5rem; min-width: 0; overflow-anchor: none; }
  .phase-panel { min-width: 0; scroll-margin-top: 1rem; }
  .phase-panel:focus-visible { outline: 2px solid var(--accent); outline-offset: 3px; }
  .phase-panel:not(.panel-conversion):not(.panel-download) { grid-column: 1 / -1; }
  h2 { display: flex; align-items: center; gap: 0.5rem; min-height: 32px; font-size: 0.8rem; font-weight: 600; color: var(--accent-light); margin: 0; border-bottom: 1px solid var(--border-color); text-transform: uppercase; letter-spacing: 0.05em; }
  .panel-errors h2 { color: var(--danger); }
  .count { color: var(--text-muted); font-size: 0.75rem; font-variant-numeric: tabular-nums; letter-spacing: normal; }
  .phase-items { display: flex; flex-direction: column; gap: 0.25rem; margin-top: 0.4rem; }
  .phase-items[hidden] { display: none; }
  [data-task-key] { position: relative; min-width: 0; }
  .phase-empty { margin: 0; padding: 0.6rem 0; color: var(--text-muted); font-size: 0.8rem; }
  .section-toggle { width: 100%; display: flex; align-items: center; gap: 0.5rem; min-height: 32px; padding: 0; border: 0; background: none; color: inherit; font: inherit; text-transform: inherit; letter-spacing: inherit; text-align: left; cursor: pointer; }
  .section-toggle > span:last-child { margin-left: auto; }
  .show-all { align-self: flex-start; min-height: 32px; padding: 0.25rem 0; border: 0; background: none; color: var(--text-secondary); font-size: 0.78rem; cursor: pointer; }
  @media (any-pointer: coarse) {
    .section-toggle, .show-all { min-height: 44px; }
  }
  @media (max-width: 768px) {
    .phase-board { grid-template-columns: minmax(0, 1fr); gap: 0.75rem; }
  }
</style>
