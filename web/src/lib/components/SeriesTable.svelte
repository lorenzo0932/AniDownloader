<script>
  // Vista tabella densa per la gestione: righe con miniatura + nome +
  // riga gestionale (servizio/episodi/ultimo download/badge) + azioni.
  // Stessi callback della griglia. Niente badge "continua": nel backend
  // e' true di default, apparirebbe su quasi ogni riga (= rumore);
  // resta nel modale dettaglio insieme a path/URL.
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

<div class="series-table" role="table" aria-label="Serie">
  {#each items as item, idx (item._file_index ?? idx)}
    <div class="series-row" style="--i:{idx}" role="button" tabindex="0"
      onclick={() => onopen?.(item._file_index)} onkeydown={(e) => e.key === 'Enter' && onopen?.(item._file_index)}>
      <img class="row-poster" src={posterUrl(item.path, 96)} alt="" loading="lazy" decoding="async" width="40" height="56" />
      <div class="row-info">
        <span class="row-name">{item.name || item.title}</span>
        <span class="row-meta">
          <span class="row-meta-text">
            {#if item.service}
              <span class="row-service">{item.service === 'animeU_scraper' ? 'AnimeU' : 'AnimeW'}</span>
            {/if}
            <span class="row-ep">Ep: {item.local_episode_count ?? '?'}</span>
            {#if downloadDate(item.last_downloaded_at)}
              <span class="row-date" title="Ultimo download">{downloadDate(item.last_downloaded_at)}</span>
            {/if}
          </span>
          {#if item.is_high_priority}
            <span class="row-badge row-badge-warn">Alta Priorità</span>
          {/if}
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
    /* Ingresso come le righe TaskItem/StatusPage (stesso rowIn + stagger). */
    animation: rowIn 0.3s ease-out both;
    animation-delay: calc(var(--i, 0) * 20ms);
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
    min-width: 0; overflow: hidden; white-space: nowrap;
  }
  /* Testo ellissizzabile (una riga, mai a capo): i separatori '·' vivono
     in CSS sui fratelli, cosi nascondere un pezzo (es. data su mobile)
     non lascia mai puntini orfani. */
  .row-meta-text {
    display: flex; align-items: center;
    min-width: 0; overflow: hidden; white-space: nowrap;
  }
  .row-meta-text > span { flex-shrink: 0; }
  .row-meta-text > span + span::before {
    content: '·'; margin: 0 0.35rem; color: var(--text-muted-more, var(--text-muted));
  }
  .row-meta-text > .row-date {
    flex-shrink: 1; overflow: hidden; text-overflow: ellipsis; min-width: 0;
  }
  .row-service { color: var(--accent); }
  .row-date { color: var(--text-muted); }
  .row-badge {
    font-size: 0.66rem; font-weight: 600; padding: 0.08rem 0.4rem;
    border-radius: 20px; background: var(--bg-tertiary); color: var(--text-muted);
    white-space: nowrap; flex-shrink: 0;
  }
  .row-badge-warn { color: var(--warning-text); background: var(--warning-bg); }
  @media (max-width: 560px) {
    /* Precedente Jellyfin mobile: via le info secondarie. La data resta
       su tablet/desktop e comunque nel modale dettaglio. */
    .row-meta-text > .row-date { display: none; }
  }
  .row-actions { display: flex; gap: 0.25rem; flex-shrink: 0; }
  .btn-row {
    background: none; border: none; color: var(--text-muted);
    padding: 0.45rem; cursor: pointer; border-radius: 6px;
    display: flex; align-items: center; justify-content: center;
  }
  .btn-row:hover { color: var(--accent); background: var(--bg-tertiary); }
  .btn-row.danger:hover { color: var(--danger); }

  @keyframes rowIn {
    from { opacity: 0; transform: translateX(-8px); }
    to { opacity: 1; transform: translateX(0); }
  }
</style>
