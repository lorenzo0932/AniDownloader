<script>
  // Vista tabella densa per la gestione: righe con miniatura + nome +
  // riga gestionale (servizio/episodi/ultimo download/badge) + azioni.
  // Stessi callback della griglia. Contenuto di dominio Gestione sopra le
  // shell condivise (SeriesCollection per il contenitore, SeriesRow per la
  // riga). Niente badge "continua": nel backend e' true di default,
  // apparirebbe su quasi ogni riga (= rumore); resta nel modale dettaglio
  // insieme a path/URL.
  import SeriesCollection from './SeriesCollection.svelte';
  import SeriesRow from './SeriesRow.svelte';
  import { posterUrl } from '../api.js';

  let { items = [], onopen, onedit, onremove } = $props();

  // Ultimo download in forma breve gg/mm/aaaa; stringhe vuote o non
  // valide -> nascosto (la riga resta densa).
  function downloadDate(v) {
    if (!v) return '';
    const d = new Date(v);
    if (Number.isNaN(d.getTime())) return '';
    return d.toLocaleDateString('it-IT', { day: '2-digit', month: '2-digit', year: 'numeric' });
  }
</script>

<SeriesCollection
  {items}
  keyOf={(item, idx) => item._file_index ?? idx}
  viewMode="table"
  emptyText=""
>
  {#snippet row(item, idx)}
    <SeriesRow
      title={item.name || item.title}
      thumb={posterUrl(item.path, 96)}
      index={idx}
      onopen={() => onopen?.(item._file_index)}
    >
      {#snippet meta()}
        {#if item.service}
          <span class="row-service">{item.service === 'animeU_scraper' ? 'AnimeU' : 'AnimeW'}</span>
        {/if}
        <span class="row-ep">Ep: {item.local_episode_count ?? '?'}</span>
        {#if downloadDate(item.last_downloaded_at)}
          <span class="row-date" title="Ultimo download">{downloadDate(item.last_downloaded_at)}</span>
        {/if}
      {/snippet}
      {#snippet badge()}
        {#if item.is_high_priority}
          <span class="row-badge row-badge-warn">Alta Priorità</span>
        {/if}
      {/snippet}
      {#snippet actions()}
        <button type="button" class="btn-row action-edit" title="Modifica" aria-label="Modifica"
          onclick={(e) => { e.stopPropagation(); onedit?.(item._file_index); }}>
          <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" width="14" height="14"><path d="M11 4H4a2 2 0 00-2 2v14a2 2 0 002-2v-7"/><path d="M18.5 2.5a2.121 2.121 0 013 3L12 15l-4 1 1-4 9.5-9.5z"/></svg>
        </button>
        <button type="button" class="btn-row danger action-delete" title="Elimina" aria-label="Elimina"
          onclick={(e) => { e.stopPropagation(); onremove?.(item._file_index); }}>
          <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" width="14" height="14"><polyline points="3 6 5 6 21 6"/><path d="M19 6v14a2 2 0 01-2 2H7a2 2 0 01-2-2V6m3 0V4a2 2 0 012-2h4a2 2 0 012 2v2"/><line x1="10" y1="11" x2="10" y2="17"/><line x1="14" y1="11" x2="14" y2="17"/></svg>
        </button>
      {/snippet}
    </SeriesRow>
  {/snippet}
</SeriesCollection>
