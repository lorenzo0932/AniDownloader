<script>
  // Carta singola della griglia serie. Il compattamento (vista compact) è un
  // prop esplicito: prima era un selettore discendente (.grid-compact) che con
  // la scoped-CSS di Svelte non attraverserebbe il confine del componente.
  let { item, index = 0, description = '', poster = '', compact = false, onopen, onedit, onremove } = $props();
</script>

<div class="series-card" class:compact style="--i:{index}" role="button" tabindex="0" onclick={() => onopen?.()} onkeydown={(e) => e.key === 'Enter' && onopen?.()}>
  <div class="card-poster-wrap">
    <img class="card-poster" src={poster} alt="" loading="lazy" />
    {#if description}
      <div class="card-desc-overlay">
        <p class="card-desc">{description}</p>
      </div>
    {/if}
  </div>
  <div class="card-body">
    <h3>{item.name || item.title}</h3>
    <p class="card-path" title={item.path}>{item.path}</p>
    <p class="card-url" title={item.series_page_url || item.url}>{item.series_page_url || item.url}</p>
    <div class="card-meta-row">
      {#if item.service}
        <span class="card-service">{item.service === 'animeU_scraper' ? 'AnimeU' : 'AnimeW'}</span>
      {/if}
      <span class="card-epcount">Ep: {item.local_episode_count ?? '?'}</span>
    </div>
  </div>
  <div class="card-actions">
    <button class="btn-card" onclick={(e) => { e.stopPropagation(); onedit?.(); }}>
      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" width="14" height="14"><path d="M11 4H4a2 2 0 00-2 2v14a2 2 0 002 2h14a2 2 0 002-2v-7"/><path d="M18.5 2.5a2.121 2.121 0 013 3L12 15l-4 1 1-4 9.5-9.5z"/></svg>
      Modifica
    </button>
    <button class="btn-card btn-card-danger" onclick={(e) => { e.stopPropagation(); onremove?.(); }}>
      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" width="14" height="14"><polyline points="3 6 5 6 21 6"/><path d="M19 6v14a2 2 0 01-2 2H7a2 2 0 01-2-2V6m3 0V4a2 2 0 012-2h4a2 2 0 012 2v2"/><line x1="10" y1="11" x2="10" y2="17"/><line x1="14" y1="11" x2="14" y2="17"/></svg>
      Elimina
    </button>
  </div>
</div>

<style>
  .series-card {
    background: var(--bg-secondary); border: 1px solid var(--border-color); border-radius: 12px; overflow: hidden;
    display: flex; flex-direction: column;
    transition: transform 0.2s ease, box-shadow 0.2s ease;
    animation: cardIn 0.35s ease-out both;
    animation-delay: calc(var(--i, 0) * 40ms);
  }
  @media (hover: hover) {
    .series-card:hover {
      transform: scale(1);
      box-shadow: 0 8px 30px rgba(0,0,0,0.35);
      z-index: 2;
    }
  }
  .card-poster-wrap {
    position: relative;
    overflow: hidden;
    cursor: pointer;
  }
  .card-poster {
    width: 100%; aspect-ratio: 2 / 3; object-fit: cover;
    background: var(--bg-tertiary);
    display: block;
    transform: translateZ(0);
  }
  .card-desc-overlay {
    position: absolute;
    inset: 0;
    background: linear-gradient(to top, rgba(0,0,0,0.92) 0%, rgba(0,0,0,0.6) 40%, rgba(0,0,0,0.2) 100%);
    display: flex;
    align-items: flex-end;
    padding: 0.75rem;
    opacity: 0;
    transition: opacity 0.25s ease;
  }
  @media (hover: hover) {
    .series-card:hover .card-desc-overlay {
      opacity: 1;
    }
  }
  .card-desc {
    color: #e0e0e0;
    font-size: 0.78rem;
    line-height: 1.45;
    display: -webkit-box; -webkit-line-clamp: 8; -webkit-box-orient: vertical;
    overflow: hidden;
  }
  .card-body { padding: 0.6rem 0.75rem; flex: 1; }
  .card-body h3 { font-size: 0.9rem; font-weight: 600; margin-bottom: 0.25rem; color: var(--text-primary); }
  .card-path { font-size: 0.72rem; color: var(--text-muted-more); white-space: nowrap; overflow: hidden; text-overflow: ellipsis; margin-bottom: 0.15rem; }
  .card-url { font-size: 0.7rem; color: var(--text-muted-more); white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
  .card-meta-row { display: flex; align-items: center; gap: 0.4rem; margin-top: 0.3rem; }
  .card-service {
    display: inline-block; font-size: 0.68rem; color: var(--accent);
    background: var(--card-service-bg); padding: 0.12rem 0.45rem; border-radius: 4px;
  }
  .card-epcount { font-size: 0.68rem; color: var(--text-muted); }
  .card-actions {
    display: flex; gap: 0; border-top: 1px solid var(--border-color);
  }
  .btn-card {
    flex: 1; display: flex; align-items: center; justify-content: center; gap: 0.3rem;
    background: none; border: none; color: var(--accent); padding: 0.55rem;
    cursor: pointer; font-size: 0.8rem; transition: all 0.1s;
  }
  .btn-card:hover { background: var(--bg-tertiary); }
  .btn-card + .btn-card { border-left: 1px solid var(--border-color); }
  .btn-card-danger { color: var(--danger); }
  .btn-card-danger:hover { background: var(--danger-bg); }

  /* Vista compact (ex .grid-compact nel parent): ora prop esplicita */
  .series-card.compact .card-poster { aspect-ratio: 2/3; }
  .series-card.compact .card-body h3 { font-size: 0.78rem; }
  .series-card.compact .card-path { display: none; }
  .series-card.compact .card-url { display: none; }
  .series-card.compact .card-service { font-size: 0.62rem; }
  .series-card.compact .card-epcount { font-size: 0.62rem; }
  .series-card.compact .card-actions .btn-card { padding: 0.4rem; font-size: 0.72rem; gap: 0.2rem; }
  .series-card.compact .btn-card svg { width: 12px; height: 12px; }

  @keyframes cardIn {
    from { opacity: 0; transform: translateY(12px); }
    to { opacity: 1; transform: translateY(0); }
  }

  @media (max-width: 768px) {
    .series-card:hover { transform: none; box-shadow: none; }
    .card-desc-overlay {
      background: linear-gradient(to top, rgba(0,0,0,0.95) 0%, rgba(0,0,0,0.7) 50%, rgba(0,0,0,0.3) 100%);
    }
  }
</style>
