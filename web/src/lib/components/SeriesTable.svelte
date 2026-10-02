<script>
  // Vista tabella densa per la gestione: righe con miniatura + nome +
  // servizio/episodi + azioni icona. Stessi callback della griglia.
  import { posterUrl } from '../api.js';

  let { items = [], onopen, onedit, onremove } = $props();
</script>

<div class="series-table" role="table" aria-label="Serie">
  {#each items as item, idx (item._file_index ?? idx)}
    <div class="series-row" role="button" tabindex="0"
      onclick={() => onopen?.(item._file_index)} onkeydown={(e) => e.key === 'Enter' && onopen?.(item._file_index)}>
      <img class="row-poster" src={posterUrl(item.path, 96)} alt="" loading="lazy" decoding="async" width="40" height="56" />
      <div class="row-info">
        <span class="row-name">{item.name || item.title}</span>
        <span class="row-meta">
          {#if item.service}
            <span class="row-service">{item.service === 'animeU_scraper' ? 'AnimeU' : 'AnimeW'}</span>
            <span aria-hidden="true">·</span>
          {/if}
          Ep: {item.local_episode_count ?? '?'}
        </span>
      </div>
      <div class="row-actions">
        <button type="button" class="btn-row" title="Modifica" aria-label="Modifica"
          onclick={(e) => { e.stopPropagation(); onedit?.(item._file_index); }}>
          <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" width="14" height="14"><path d="M11 4H4a2 2 0 00-2 2v14a2 2 0 002 2h14a2 2 0 002-2v-7"/><path d="M18.5 2.5a2.121 2.121 0 013 3L12 15l-4 1 1-4 9.5-9.5z"/></svg>
        </button>
        <button type="button" class="btn-row danger" title="Elimina" aria-label="Elimina"
          onclick={(e) => { e.stopPropagation(); onremove?.(item._file_index); }}>
          <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" width="14" height="14"><polyline points="3 6 5 6 21 6"/><path d="M19 6v14a2 2 0 01-2 2H7a2 2 0 01-2-2V6m3 0V4a2 2 0 012-2h4a2 2 0 012 2v2"/><line x1="10" y1="11" x2="10" y2="17"/><line x1="14" y1="11" x2="14" y2="17"/></svg>
        </button>
      </div>
    </div>
  {/each}
</div>

<style>
  .series-table {
    display: flex; flex-direction: column; gap: 0.4rem;
  }
  .series-row {
    display: flex; align-items: center; gap: 0.75rem;
    background: var(--bg-secondary); border: 1px solid var(--border-color);
    border-radius: 10px; padding: 0.4rem 0.6rem;
    cursor: pointer;
    transition: border-color 0.15s ease;
  }
  @media (hover: hover) {
    .series-row:hover { border-color: var(--accent); }
  }
  .row-poster {
    width: 40px; height: 56px; border-radius: 4px; object-fit: contain;
    background: var(--bg-tertiary); flex-shrink: 0;
  }
  .row-info {
    flex: 1; min-width: 0;
    display: flex; flex-direction: column; gap: 0.15rem;
  }
  .row-name {
    font-size: 0.88rem; font-weight: 600; color: var(--text-primary);
    white-space: nowrap; overflow: hidden; text-overflow: ellipsis;
  }
  .row-meta {
    font-size: 0.72rem; color: var(--text-muted);
    display: flex; align-items: center; gap: 0.35rem;
  }
  .row-service { color: var(--accent); }
  .row-actions { display: flex; gap: 0.25rem; flex-shrink: 0; }
  .btn-row {
    background: none; border: none; color: var(--text-muted);
    padding: 0.45rem; cursor: pointer; border-radius: 6px;
    display: flex; align-items: center; justify-content: center;
  }
  .btn-row:hover { color: var(--accent); background: var(--bg-tertiary); }
  .btn-row.danger:hover { color: var(--danger); }
</style>
