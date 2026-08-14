<script>
  // Modale dettaglio serie. Lo stato di apertura (detailIndex) resta nel
  // parent: qui solo presentazione della serie + azioni.
  let { series = null, description = '', poster = '', onclose, onedit, onremove } = $props();
</script>

{#if series}
  <div class="modal-overlay" onclick={onclose} onkeydown={(e) => e.key === 'Escape' && onclose()} role="dialog" aria-modal="true" tabindex="-1">
    <div class="modal-panel detail-modal" onclick={(e) => e.stopPropagation()} role="presentation">
      <div class="detail-modal-inner">
        <div class="detail-poster-col">
          <img class="detail-poster" src={poster} alt="" loading="lazy" />
        </div>
        <div class="detail-info-col">
          <button class="detail-close" onclick={onclose}>&times;</button>
          <div class="detail-info-scroll">
            <h2 class="detail-title">{series.name || series.title}</h2>
            <div class="detail-meta">
              {#if series.service}
                <span class="card-service">{series.service === 'animeU_scraper' ? 'AnimeU' : 'AnimeW'}</span>
              {/if}
              {#if series.continue}
                <span class="detail-badge">Continua numerazione</span>
              {/if}
              {#if series.is_high_priority}
                <span class="detail-badge detail-badge-warn">Alta Priorit&agrave;</span>
              {/if}
            </div>
            {#if description}
              <div class="detail-desc">
                <p>{description}</p>
              </div>
            {:else}
              <div class="detail-desc detail-desc-empty">Nessuna descrizione disponibile.</div>
            {/if}
            <div class="detail-stats">
              <div class="detail-stat">
                <span class="detail-stat-label">Episodi in locale</span>
                <span class="detail-stat-value">
                  {series.local_episode_count ?? '?'}
                  {#if (series.passed_episodes ?? 0) > 0 && series.local_episode_count !== series.passed_episodes}
                    <span class="detail-stat-diff">(config: {series.passed_episodes})</span>
                  {/if}
                </span>
              </div>
              <div class="detail-stat">
                <span class="detail-stat-label">Percorso</span>
                <span class="detail-stat-value detail-stat-path" title={series.path}>{series.path || '—'}</span>
              </div>
              <div class="detail-stat">
                <span class="detail-stat-label">URL</span>
                {#if series.series_page_url || series.url}
                  <a class="detail-stat-value detail-stat-url" href={series.series_page_url || series.url} target="_blank" rel="noopener noreferrer" title={series.series_page_url || series.url}>{series.series_page_url || series.url}</a>
                {:else}
                  <span class="detail-stat-value detail-stat-url">—</span>
                {/if}
              </div>
            </div>
          </div>
          <div class="detail-actions">
            <button class="btn-primary" onclick={onedit}>
              <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" width="14" height="14"><path d="M11 4H4a2 2 0 00-2 2v14a2 2 0 002 2h14a2 2 0 002-2v-7"/><path d="M18.5 2.5a2.121 2.121 0 013 3L12 15l-4 1 1-4 9.5-9.5z"/></svg>
              Modifica
            </button>
            <button class="btn-danger" onclick={onremove}>
              <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" width="14" height="14"><polyline points="3 6 5 6 21 6"/><path d="M19 6v14a2 2 0 01-2 2H7a2 2 0 01-2-2V6m3 0V4a2 2 0 012-2h4a2 2 0 012 2v2"/><line x1="10" y1="11" x2="10" y2="17"/><line x1="14" y1="11" x2="14" y2="17"/></svg>
              Elimina
            </button>
          </div>
        </div>
      </div>
    </div>
  </div>
{/if}

<style>
  .modal-overlay {
    position: fixed; top: 0; left: 0; right: 0; bottom: 0;
    display: flex; align-items: center; justify-content: center;
    background: var(--overlay); z-index: 200;
    border: none; padding: 0; cursor: default;
    animation: fadeIn 0.2s ease;
  }
  .detail-modal {
    animation: scaleIn 0.25s ease-out;
    max-width: 90vw; max-height: 90vh;
  }
  .detail-modal-inner {
    display: flex; gap: 0;
    background: var(--bg-secondary); border: 1px solid var(--border-color);
    border-radius: 16px; overflow: hidden;
    max-height: 80vh;
  }
  .detail-poster-col {
    flex-shrink: 0;
  }
  .detail-poster {
    width: 260px; height: 100%; aspect-ratio: 2/3; object-fit: cover;
    display: block;
    transform: translateZ(0);
  }
  .detail-info-col {
    flex: 1; min-width: 0; padding: 1.5rem;
    display: flex; flex-direction: column;
    position: relative; max-width: 460px;
  }
  .detail-info-scroll {
    flex: 1; overflow-y: auto;
    display: flex; flex-direction: column; gap: 0.75rem;
  }
  .detail-close {
    position: absolute; top: 0.75rem; right: 0.75rem;
    background: none; border: none; color: var(--text-muted);
    font-size: 1.5rem; cursor: pointer; line-height: 1;
    padding: 0.25rem;
  }
  .detail-close:hover { color: var(--text-primary); }
  .detail-title { font-size: 1.15rem; font-weight: 700; color: var(--text-primary); padding-right: 2rem; }
  .detail-meta { display: flex; flex-wrap: wrap; gap: 0.4rem; align-items: center; }
  .card-service {
    display: inline-block; font-size: 0.68rem; color: var(--accent);
    background: var(--card-service-bg); padding: 0.12rem 0.45rem; border-radius: 4px;
  }
  .detail-badge {
    font-size: 0.72rem; color: var(--success); background: var(--success-border);
    padding: 0.12rem 0.5rem; border-radius: 4px; font-weight: 600;
  }
  .detail-badge-warn { color: var(--warning-text); background: var(--warning-bg); }
  .detail-desc {
    font-size: 0.85rem; color: var(--text-secondary); line-height: 1.6;
    max-height: 200px; overflow-y: auto;
    animation: slideUp 0.3s ease-out;
  }
  .detail-desc-empty { color: var(--text-muted); font-style: italic; }
  .detail-stats { display: flex; flex-direction: column; gap: 0.5rem; }
  .detail-stat { display: flex; justify-content: space-between; align-items: center; }
  .detail-stat-label { font-size: 0.75rem; color: var(--text-muted); }
  .detail-stat-value { font-size: 0.85rem; color: var(--text-primary); font-weight: 600; }
  .detail-stat-path { font-size: 0.78rem; word-break: break-all; text-align: right; max-width: 55%; }
  .detail-stat-url { font-size: 0.75rem; font-weight: 400; word-break: break-all; text-align: right; max-width: 55%; }
  .detail-stat-url:hover { color: var(--accent); }
  .detail-actions { display: flex; gap: 0.5rem; padding-top: 0.75rem; border-top: 1px solid var(--border-color); flex-shrink: 0;
    padding-bottom: 96px;
  }
  .detail-stat-diff { color: var(--text-muted); font-size: 0.75rem; margin-left: 0.4rem; }
  .btn-primary {
    padding: 0.55rem 1.1rem; background: var(--accent); border: none; border-radius: 8px;
    color: #fff; font-size: 0.85rem; font-weight: 600; cursor: pointer;
    display: flex; align-items: center; gap: 0.4rem;
  }
  .btn-primary:hover { background: var(--accent-hover); }
  .btn-danger {
    padding: 0.55rem 1.1rem; background: var(--danger); border: none; border-radius: 8px;
    color: #fff; font-size: 0.85rem; font-weight: 600; cursor: pointer;
    display: flex; align-items: center; gap: 0.4rem;
  }
  .btn-danger:hover { opacity: 0.85; }

  @keyframes fadeIn { from { opacity: 0; } to { opacity: 1; } }
  @keyframes scaleIn { from { opacity: 0; transform: scale(0.95); } to { opacity: 1; transform: scale(1); } }
  @keyframes slideUp { from { opacity: 0; transform: translateY(10px); } to { opacity: 1; transform: translateY(0); } }

  @media (max-width: 768px) {
    .detail-modal {
      position: fixed; top: 0; left: 0; right: 0; bottom: 0;
      max-width: 100vw; max-height: 100dvh; border-radius: 0;
      animation: fadeIn 0.2s ease;
    }
    .detail-modal-inner {
      flex-direction: column; border-radius: 0;
      max-height: 100dvh; height: 100dvh;
    }
    .detail-poster { width: 100%; aspect-ratio: 2/3; max-height: 40dvh; object-fit: cover; }
    .detail-info-col { max-width: none; padding: 1rem; }
    .detail-desc { max-height: none; }
    .detail-stat-path { max-width: none; }
    .detail-stat-url { max-width: none; }
  }

  @media (orientation: landscape) and (max-height: 520px) {
    .detail-modal {
      position: fixed; top: 0; left: 0; right: 0; bottom: 0;
      max-width: 100vw; max-height: 100dvh; border-radius: 0;
    }
    .detail-modal-inner {
      flex-direction: row; border-radius: 0;
      max-height: 100dvh; height: 100dvh;
    }
    .detail-poster { width: 160px; max-height: 100dvh; aspect-ratio: 2/3; object-fit: cover; }
    .detail-info-col { max-width: none; padding: 0.75rem; }
    .detail-actions { padding-bottom: 48px; }
  }
</style>
