<script>
  // Griglia delle carte serie (con stato vuoto). Il caricamento/ordinamento
  // resta nel parent (DashboardPage): qui solo presentazione.
  import SeriesCard from './SeriesCard.svelte';
  import { posterUrl } from '../api.js';

  let { items = [], viewMode = 'normal', totalCount = 0, descriptions = {}, onopen, onedit, onremove } = $props();
</script>

{#if items.length === 0}
  <div class="empty">{totalCount === 0 ? 'Nessuna serie configurata. Aggiungine una per iniziare!' : 'Nessuna serie corrisponde alla ricerca.'}</div>
{:else}
  <div class="series-grid" class:grid-compact={viewMode === 'compact'}>
    {#each items as item, idx (item._file_index)}
      <SeriesCard
        {item}
        index={idx}
        description={descriptions[item._file_index]}
        poster={posterUrl(item.path)}
        compact={viewMode === 'compact'}
        onopen={() => onopen(item._file_index)}
        onedit={() => onedit(item._file_index)}
        onremove={() => onremove(item._file_index)}
      />
    {/each}
  </div>
{/if}

<style>
  .empty { text-align: center; padding: 3rem; color: var(--text-muted); font-size: 0.9rem; }

  .series-grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(220px, 1fr)); gap: 1rem; }
  .grid-compact { grid-template-columns: repeat(auto-fill, minmax(140px, 1fr)); gap: 0.75rem; }

  @media (max-width: 768px) {
    .series-grid { grid-template-columns: 1fr; }
    .series-grid.grid-compact { grid-template-columns: repeat(2, 1fr); }
  }
</style>
