<script>
  // Griglia delle carte serie (con stato vuoto) oppure vista tabella densa.
  // Il caricamento/ordinamento resta nel parent (DashboardPage): qui solo
  // orchestrazione di dominio Gestione sopra il layout condiviso
  // (SeriesCollection) e le shell condivise (SeriesCard/SeriesTable).
  // viewMode: 'grid' | 'table' | 'large'.
  import SeriesCollection from './SeriesCollection.svelte';
  import SeriesCard from './SeriesCard.svelte';
  import SeriesTable from './SeriesTable.svelte';
  import { posterUrl, posterSrcSet } from '../api.js';

  let { items = [], viewMode = 'grid', totalCount = 0, descriptions = {}, onopen, onedit, onremove } = $props();
  // 'large' = stessa griglia con colonne grandi (220px, poster ~330px).
  // $derived: const calcolerebbe una volta sola e il toggle sembrerebbe morto.
  let large = $derived(viewMode === 'large');
  let emptyText = $derived(totalCount === 0
    ? 'Nessuna serie configurata. Aggiungine una per iniziare!'
    : 'Nessuna serie corrisponde alla ricerca.');

  function serviceName(service) {
    return service === 'animeU_scraper' ? 'AnimeU' : 'AnimeW';
  }
</script>

{#if viewMode === 'table'}
  {#if items.length === 0}
    <div class="empty">{emptyText}</div>
  {:else}
    <SeriesTable
      {items}
      {onopen}
      {onedit}
      {onremove}
    />
  {/if}
{:else}
  <SeriesCollection
    {items}
    keyOf={(item) => item._file_index}
    {viewMode}
    {emptyText}
  >
    {#snippet card(item, idx)}
      <SeriesCard
        title={item.name || item.title}
        poster={posterUrl(item.path)}
        srcset={posterSrcSet(item.path)}
        {large}
        index={idx}
        onopen={() => onopen(item._file_index)}
      >
        {#snippet meta()}
          {#if item.service}
            <span class="card-service">{serviceName(item.service)}</span>
          {/if}
          <span class="card-epcount">Ep: {item.local_episode_count ?? '?'}</span>
        {/snippet}
        {#snippet actions()}
          <button type="button" class="btn-hover" title="Modifica" aria-label="Modifica"
            onclick={(e) => { e.stopPropagation(); onedit?.(item._file_index); }}>
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M11 4H4a2 2 0 00-2 2v14a2 2 0 002-2v-7"/><path d="M18.5 2.5a2.121 2.121 0 013 3L12 15l-4 1 1-4 9.5-9.5z"/></svg>
          </button>
          <button type="button" class="btn-hover danger" title="Elimina" aria-label="Elimina"
            onclick={(e) => { e.stopPropagation(); onremove?.(item._file_index); }}>
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><polyline points="3 6 5 6 21 6"/><path d="M19 6v14a2 2 0 01-2 2H7a2 2 0 01-2-2V6m3 0V4a2 2 0 012-2h4a2 2 0 012 2v2"/><line x1="10" y1="11" x2="10" y2="17"/><line x1="14" y1="11" x2="14" y2="17"/></svg>
          </button>
        {/snippet}
      </SeriesCard>
    {/snippet}
  </SeriesCollection>
{/if}

<style>
  .empty { text-align: center; padding: 3rem; color: var(--text-muted); font-size: 0.9rem; }
</style>
