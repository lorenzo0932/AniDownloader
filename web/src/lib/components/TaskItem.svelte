<script>
  // Riga singola della lista progressi (analisi/attiva/completata/saltata/attesa).
  // La variante determina markup e stile; task (per le varianti di attività) o
  // series (per analisi/attesa) portano i dati.
  let { variant = 'active', task = null, series = null, poster = '', index = 0 } = $props();
</script>

{#if variant === 'analysing' && series}
  <div class="series-row analysing" style="--i:{index}">
    <img class="poster-thumb" src={poster} alt="" loading="lazy" />
    <div class="series-info">
      <span class="series-name">{series.name}</span>
      <span class="series-status">Analisi...</span>
    </div>
  </div>
{:else if variant === 'active' && task}
  <div class="series-row active" style="--i:{index}">
    <div class="series-info">
      <span class="series-name">{task.seriesName}{task.episode > 0 ? ` (Ep ${task.episode})` : ''}</span>
      <span class="series-status">{task.statusText}</span>
    </div>
    <div class="pbar-wrap">
      <div class="pbar">
        <div class="pbar-fill" style="width:{task.percent}%"></div>
      </div>
      <span class="pbar-text">{Math.round(task.percent)}%</span>
    </div>
  </div>
{:else if variant === 'done' && task}
  <div class="series-row done" style="--i:{index}">
    <div class="series-info">
      <span class="series-name">{task.seriesName}{task.episode > 0 ? ` (Ep ${task.episode})` : ''}</span>
      <span class="series-status ok">{task.statusText}</span>
    </div>
  </div>
{:else if variant === 'skipped' && task}
  <div class="series-row skipped" style="--i:{index}">
    <div class="series-info">
      <span class="series-name">{task.seriesName}{task.episode > 0 ? ` (Ep ${task.episode})` : ''}</span>
      <span class="series-status skip">{task.statusText}</span>
    </div>
  </div>
{:else if variant === 'waiting' && series}
  <div class="series-row waiting" style="--i:{index}">
    <div class="series-info">
      <span class="series-name">{series.name}</span>
    </div>
  </div>
{/if}

<style>
  .series-row {
    display: flex; align-items: center; gap: 0.75rem;
    background: var(--bg-secondary); border-radius: 8px; padding: 0.5rem 0.75rem;
    border: 1px solid var(--border-color);
    animation: rowIn 0.3s ease-out both;
    animation-delay: calc(var(--i, 0) * 20ms);
  }
  .series-row.active { border-color: var(--accent-bg); }
  .series-row.analysing { opacity: 0.65; }
  .series-row.done { opacity: 0.7; }
  .series-row.skipped { opacity: 0.5; }
  .series-row.waiting { opacity: 0.4; }

  .poster-thumb {
    width: 40px; height: 56px; border-radius: 4px; object-fit: cover;
    background: var(--bg-tertiary); flex-shrink: 0;
  }
  .series-info { flex: 1; min-width: 0; display: flex; flex-direction: column; gap: 0.15rem; }
  .series-name { font-size: 0.85rem; font-weight: 500; color: var(--text-primary); white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
  .series-status { font-size: 0.75rem; color: var(--text-muted); }
  .series-status.ok { color: var(--success); }
  .series-status.skip { color: var(--warning); }

  .pbar-wrap { display: flex; align-items: center; gap: 0.5rem; flex-shrink: 0; min-width: 140px; }
  .pbar { flex: 1; height: 8px; background: var(--border-color); border-radius: 4px; overflow: hidden; }
  .pbar-fill {
    height: 100%; background: var(--accent); border-radius: 4px; transition: width 0.3s ease;
  }
  .pbar-text { font-size: 0.75rem; color: var(--accent-light); width: 2.5rem; text-align: right; }

  @keyframes rowIn {
    from { opacity: 0; transform: translateX(-8px); }
    to { opacity: 1; transform: translateX(0); }
  }

  @media (max-width: 768px) {
    .series-row { padding: 0.4rem 0.5rem; gap: 0.5rem; flex-wrap: nowrap; }
    .poster-thumb { width: 32px; height: 44px; }
    .pbar-wrap { min-width: 100px; }
    .pbar-text { width: 2rem; font-size: 0.7rem; }
    .series-name { font-size: 0.8rem; }
    .series-status { font-size: 0.7rem; }
  }
</style>
