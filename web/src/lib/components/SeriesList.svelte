<script>
  // Griglia delle carte serie (con stato vuoto) oppure vista tabella densa.
  // Il caricamento/ordinamento resta nel parent (DashboardPage): qui solo
  // presentazione. viewMode: 'grid' | 'table' | 'large'.
  import SeriesCard from './SeriesCard.svelte';
  import SeriesTable from './SeriesTable.svelte';
  import { flip } from 'svelte/animate';
  import { posterUrl, posterSrcSet } from '../api.js';

  let { items = [], viewMode = 'grid', totalCount = 0, descriptions = {}, onopen, onedit, onremove } = $props();
  // 'large' = stessa griglia con colonne grandi (220px, poster ~330px).
  // $derived: const calcolerebbe una volta sola e il toggle sembrerebbe morto.
  let large = $derived(viewMode === 'large');
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
  <div class="series-grid" class:grid-large={large}>
    {#each items as item, idx (item._file_index)}
      <!-- Wrapper per animate:flip (lo standard Svelte per i relayout):
           senza, lo switch griglia<->large riuserebbe i nodi e sembrerebbe
           morto. Le card scivolano nelle nuove posizioni, niente refetch. -->
      <div class="flip-wrap" animate:flip={{ duration: 300 }}>
      <SeriesCard
        {item}
        index={idx}
        poster={posterUrl(item.path)}
        srcset={posterSrcSet(item.path)}
        {large}
        onopen={() => onopen(item._file_index)}
        onedit={() => onedit(item._file_index)}
        onremove={() => onremove(item._file_index)}
      />
      </div>
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
  /* Wrapper flip: item di griglia neutro, la card riempie. */
  .flip-wrap { min-width: 0; }
  .flip-wrap > :first-child { width: 100%; }
  /* Vista grande: stesse card, colonne da 220px (poster ~330px). */
  .grid-large {
    grid-template-columns: repeat(auto-fill, minmax(220px, 1fr));
    max-width: 1400px;
  }

  @media (max-width: 768px) {
    /* Mai una sola colonna di card portrait: una locandina 2:3 a tutta
       larghezza riempie lo schermo da sola. Minimo 2 colonne. */
    .series-grid { grid-template-columns: repeat(2, 1fr); gap: 0.75rem; }
    /* Tablet (561-768px): la large torna auto-fill e differenzia davvero
       (3 colonne dove ci stanno). Selettore piu specifico della regola
       sopra, quindi vince per la large senza dipendere dall'ordine. */
    .series-grid.grid-large {
      grid-template-columns: repeat(auto-fill, minmax(220px, 1fr));
    }
  }
  @media (max-width: 560px) {
    /* Telefono: 2 colonne da 220px non ci stanno (l'auto-fill crollerebbe
       a 1 colonna = card giganti bandite), quindi la large resta 2 colonne
       come la griglia. Identita voluta e dichiarata, non un bug. */
    .series-grid.grid-large { grid-template-columns: repeat(2, 1fr); }
  }
</style>
