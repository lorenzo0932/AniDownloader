<script>
  // Griglia delle carte serie (con stato vuoto) oppure vista tabella densa.
  // Il caricamento/ordinamento resta nel parent (DashboardPage): qui solo
  // presentazione. viewMode: 'grid' | 'table'.
  import SeriesCard from './SeriesCard.svelte';
  import SeriesTable from './SeriesTable.svelte';
  import { posterUrl, posterSrcSet } from '../api.js';

  let { items = [], viewMode = 'grid', totalCount = 0, descriptions = {}, onopen, onedit, onremove } = $props();
</script>

{#if items.length === 0}
  <div class="empty">{totalCount === 0 ? 'Nessuna serie configurata. Aggiungine una per iniziare!' : 'Nessuna serie corrisponde alla ricerca.'}</div>
{:else if viewMode === 'table'}
  <SeriesTable
    {items}
    {onopen}
    {onedit}
    {onremove}
  />
{:else}
  <div class="series-grid">
    {#each items as item, idx (item._file_index)}
      <SeriesCard
        {item}
        index={idx}
        poster={posterUrl(item.path)}
        srcset={posterSrcSet(item.path)}
        onopen={() => onopen(item._file_index)}
        onedit={() => onedit(item._file_index)}
        onremove={() => onremove(item._file_index)}
      />
    {/each}
  </div>
{/if}

<style>
  .empty { text-align: center; padding: 3rem; color: var(--text-muted); font-size: 0.9rem; }

  /* Griglia fluida con tetto: le colonne si adattano a ogni schermo
     (1fr dinamico, ratio 2:3 sempre rispettato), ma oltre ~1100px la
     griglia smette di crescere e resta centrata — niente più card
     gonfiate sui monitor larghi. */
  .series-grid {
    display: grid; grid-template-columns: repeat(auto-fill, minmax(150px, 1fr));
    gap: 0.9rem; max-width: 1100px; margin-inline: auto;
  }

  @media (max-width: 768px) {
    /* Mai una sola colonna di card portrait: una locandina 2:3 a tutta
       larghezza riempie lo schermo da sola. Minimo 2 colonne. */
    .series-grid { grid-template-columns: repeat(2, 1fr); gap: 0.75rem; }
  }
</style>
