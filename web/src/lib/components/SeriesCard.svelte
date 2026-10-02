<script>
  // Carta essenziale della griglia serie: poster + titolo + badge.
  // Path/URL e azioni vivono nel modale dettaglio; le azioni sono anche in
  // overlay hover (solo desktop con hover: su touch il tap apre il dettaglio).
  let { item, index = 0, poster = '', srcset = '', onopen, onedit, onremove } = $props();
</script>

<div class="series-card" style="--i:{index}" role="button" tabindex="0" onclick={() => onopen?.()} onkeydown={(e) => e.key === 'Enter' && onopen?.()} aria-label={item.name || item.title}>
  <div class="card-poster-wrap">
    <img class="card-poster" src={poster} srcset={srcset}
      sizes="(max-width: 768px) 50vw, 200px"
      width="480" height="720" alt="" loading="lazy" decoding="async" />
    <div class="card-hover-actions">
      <button type="button" class="btn-hover" title="Dettagli" aria-label="Dettagli"
        onclick={(e) => { e.stopPropagation(); onopen?.(); }}>
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="9"/><line x1="12" y1="11" x2="12" y2="16"/><line x1="12" y1="8" x2="12.01" y2="8"/></svg>
      </button>
      <button type="button" class="btn-hover" title="Modifica" aria-label="Modifica"
        onclick={(e) => { e.stopPropagation(); onedit?.(); }}>
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M11 4H4a2 2 0 00-2 2v14a2 2 0 002 2h14a2 2 0 002-2v-7"/><path d="M18.5 2.5a2.121 2.121 0 013 3L12 15l-4 1 1-4 9.5-9.5z"/></svg>
      </button>
      <button type="button" class="btn-hover danger" title="Elimina" aria-label="Elimina"
        onclick={(e) => { e.stopPropagation(); onremove?.(); }}>
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><polyline points="3 6 5 6 21 6"/><path d="M19 6v14a2 2 0 01-2 2H7a2 2 0 01-2-2V6m3 0V4a2 2 0 012-2h4a2 2 0 012 2v2"/><line x1="10" y1="11" x2="10" y2="17"/><line x1="14" y1="11" x2="14" y2="17"/></svg>
      </button>
    </div>
  </div>
  <div class="card-body">
    <h3>{item.name || item.title}</h3>
    <div class="card-meta-row">
      {#if item.service}
        <span class="card-service">{item.service === 'animeU_scraper' ? 'AnimeU' : 'AnimeW'}</span>
      {/if}
      <span class="card-epcount">Ep: {item.local_episode_count ?? '?'}</span>
    </div>
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
    .series-card:hover .card-hover-actions {
      opacity: 1;
    }
  }
  .series-card:focus-within .card-hover-actions {
    opacity: 1;
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
  .card-hover-actions {
    position: absolute; inset: 0;
    display: flex; align-items: center; justify-content: center; gap: 0.6rem;
    background: rgba(0,0,0,0.45);
    opacity: 0;
    transition: opacity 0.2s ease;
  }
  .btn-hover {
    width: 2.5rem; height: 2.5rem; border-radius: 50%;
    border: 1px solid rgba(255,255,255,0.35);
    background: rgba(20,20,25,0.75); color: #fff; cursor: pointer;
    display: flex; align-items: center; justify-content: center;
    transition: background 0.15s ease, border-color 0.15s ease;
  }
  .btn-hover:hover { background: var(--accent); border-color: var(--accent); }
  .btn-hover.danger:hover { background: var(--danger); border-color: var(--danger); }
  .btn-hover svg { width: 1.1rem; height: 1.1rem; }
  .card-body { padding: 0.6rem 0.75rem; flex: 1; }
  .card-body h3 { font-size: 0.9rem; font-weight: 600; margin-bottom: 0.25rem; color: var(--text-primary); }
  .card-meta-row { display: flex; align-items: center; gap: 0.4rem; margin-top: 0.3rem; }
  .card-service {
    display: inline-block; font-size: 0.68rem; color: var(--accent);
    background: var(--card-service-bg); padding: 0.12rem 0.45rem; border-radius: 4px;
  }
  .card-epcount { font-size: 0.68rem; color: var(--text-muted); }

  @keyframes cardIn {
    from { opacity: 0; transform: translateY(12px); }
    to { opacity: 1; transform: translateY(0); }
  }

  @media (max-width: 768px) {
    .series-card:hover { transform: none; box-shadow: none; }
  }
</style>
