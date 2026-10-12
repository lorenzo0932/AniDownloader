<script>
  // Layout condiviso delle collezioni serie (Gestione e Scopri): stato vuoto,
  // griglia (grid/large) oppure tabella densa. Unico proprietario di
  // contenitori, griglie e animazioni di lista. Contenuti via snippet:
  // `card(item, idx)` per la griglia, `row(item, idx)` per la tabella.
  // Caricamento/ordinamento restano nel parent: qui solo presentazione.
  import { flip } from 'svelte/animate';

  let {
    items = [],
    keyOf = (item, idx) => idx,
    viewMode = 'grid',
    emptyText = 'Nessuna serie.',
    showEmpty = true,
    card = null,
    row = null,
  } = $props();
  // 'large' = stessa griglia con colonne grandi (220px, poster ~330px).
  // $derived: const calcolerebbe una volta sola e il toggle sembrerebbe morto.
  let large = $derived(viewMode === 'large');
</script>

{#if items.length === 0}
  {#if showEmpty}<div class="empty">{emptyText}</div>{/if}
{:else if viewMode === 'table'}
  <div class="series-table" role="table" aria-label="Serie">
    {#each items as item, idx (keyOf(item, idx))}
      {@render row?.(item, idx)}
    {/each}
  </div>
{:else}
  <!-- {#key}: toggle e sort ricreano la griglia e cardIn riparte sempre.
       Senza, i nodi riusati non animano (visto su grid<->large diretto).
       Il flip resta per i riordini senza cambio vista. -->
  {#key viewMode}
  <div class="series-grid" class:grid-large={large}>
    {#each items as item, idx (keyOf(item, idx))}
      <!-- Wrapper per animate:flip (lo standard Svelte per i relayout):
           senza, lo switch griglia<->large riuserebbe i nodi e sembrerebbe
           morto. Le card scivolano nelle nuove posizioni, niente refetch. -->
      <div class="flip-wrap" animate:flip={{ duration: 300 }}>
        {@render card?.(item, idx)}
      </div>
    {/each}
  </div>
  {/key}
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
  /* Wrapper flip: item di griglia neutro, la card riempie. :global per il
     contenuto degli snippet (compilato con lo scope del consumer). */
  .flip-wrap { min-width: 0; }
  .flip-wrap > :global(*) { width: 100%; }
  /* Vista grande: stesse card, colonne da 220px (poster ~330px). */
  .grid-large {
    grid-template-columns: repeat(auto-fill, minmax(220px, 1fr));
    max-width: 1400px;
  }

  .series-table {
    display: flex; flex-direction: column; gap: 0.4rem;
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
