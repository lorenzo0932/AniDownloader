<script>
  import { tick } from 'svelte';
  import { eventDescription } from '../downloadState.js';
  let { events = [] } = $props();
  let expanded = $state(false);
  let following = $state(true);
  let view = $state();
  $effect(() => {
    const count = events.length;
    const open = expanded;
    if (count && open && following) tick().then(() => { if (view) view.scrollTop = view.scrollHeight; });
  });
  function onscroll() {
    following = view.scrollHeight - view.clientHeight - view.scrollTop < 32;
  }
  function formatTime(timestamp) {
    return new Date(timestamp).toLocaleTimeString('it-IT', { hour: '2-digit', minute: '2-digit', second: '2-digit' });
  }
</script>

{#if events.length}
  <section class="event-log">
    <h2><button aria-expanded={expanded} aria-controls="download-events" onclick={() => expanded = !expanded}>
      {expanded ? '▾' : '▸'} Eventi <span>{events.length}</span>
    </button></h2>
    {#if expanded}
      <p class="log-hint">Passaggi di fase e risultati · ultimi 120 eventi</p>
      <!-- svelte-ignore a11y_no_noninteractive_tabindex (La cronologia scorrevole deve essere raggiungibile da tastiera.) -->
      <div class="log-view" id="download-events" bind:this={view} {onscroll} tabindex="0" role="region" aria-label="Cronologia elaborazione">
        <ol>
          {#each events as event (event.id)}
            <li class:failure={['error', 'interrupted'].includes(event.task?.phase)}>
              <time datetime={new Date(event.timestamp).toISOString()}>{formatTime(event.timestamp)}</time>
              <div>
                {#if event.series}<strong>{event.series}{event.episode > 0 ? ` · Episodio ${event.episode}` : ''}</strong>{/if}
                <span>{eventDescription(event)}</span>
              </div>
            </li>
          {/each}
        </ol>
      </div>
      {#if !following}<button class="follow" onclick={() => { following = true; }}>Vai agli ultimi eventi</button>{/if}
    {/if}
  </section>
{/if}

<style>
  .event-log { border-top: 1px solid var(--border-color); padding-top: 0.5rem; }
  h2 { margin: 0; font-size: 0.85rem; }
  button { min-height: 44px; background: none; border: 0; color: var(--text-secondary); font: inherit; cursor: pointer; text-align: left; }
  h2 button { width: 100%; }
  h2 span { margin-left: 0.5rem; color: var(--text-muted); }
  .log-hint { margin: 0 0 0.5rem; font-size: 0.75rem; color: var(--text-muted); }
  .log-view { max-height: 320px; overflow-y: auto; overscroll-behavior: contain; background: var(--bg-secondary); border-radius: 8px; padding: 0.6rem; }
  ol { list-style: none; margin: 0; padding: 0; }
  li { display: grid; grid-template-columns: auto minmax(0, 1fr); gap: 0.65rem; padding: 0.5rem 0; border-bottom: 1px solid var(--border-color); font-size: 0.78rem; }
  li:last-child { border-bottom: 0; }
  time { color: var(--text-muted); font-variant-numeric: tabular-nums; }
  li div { display: flex; flex-direction: column; gap: 0.15rem; overflow-wrap: anywhere; color: var(--text-secondary); }
  strong { color: var(--text-primary); font-weight: 600; }
  .failure span { color: var(--danger); }
</style>
