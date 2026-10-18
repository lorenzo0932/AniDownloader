<script>
  // Riga singola della lista progressi (analisi/attiva/completata/saltata/attesa).
  // La variante determina markup e stile; task (per le varianti di attività) o
  // series (per analisi/attesa) portano i dati.
  let { variant = 'active', task = null, series = null, poster = '', index = 0 } = $props();
  let posterFailed = $state(false);
  $effect(() => { poster; posterFailed = false; });
</script>

{#if variant === 'phase' && task}
  <article class="task-card phase-{task.phase}">
    {#if poster && !posterFailed}
      <img class="task-poster" src={poster} alt="" loading="lazy" decoding="async" width="40" height="56" onerror={() => posterFailed = true} />
    {:else}
      <span class="task-poster placeholder" aria-hidden="true">
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5"><rect x="4" y="3" width="16" height="18" rx="2"/><path d="m4 16 5-5 4 4 3-3 4 4"/></svg>
      </span>
    {/if}
    <div class="task-content">
      <div class="task-title" title={task.seriesName}>{task.seriesName}</div>
      <div class="task-episode">{task.episode > 0 ? `Episodio ${task.episode}` : 'Serie'}</div>
      {#if ['downloading', 'converting'].includes(task.phase) && task.percent >= 0}
        <div class="task-progress">
          <progress max="100" value={task.percent} aria-label={`${task.seriesName}: ${task.phase === 'downloading' ? 'download' : 'conversione'}`}></progress>
          <span>{Math.round(task.percent)}%</span>
        </div>
      {:else if task.phase === 'waiting_conversion'}
        <p class="task-detail ready">✓ Scaricato</p>
      {:else if task.phase === 'completed'}
        <p class="task-detail ready">✓ Pronto</p>
      {:else if task.detail}
        <p class="task-detail">{task.detail}</p>
      {/if}
    </div>
  </article>
{:else if variant === 'analysing' && series}
  <div class="series-row analysing" style="--i:{index}">
    <img class="poster-thumb" src={poster} alt="" loading="lazy" decoding="async" width="40" height="56" />
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
  .task-card { display: flex; align-items: center; gap: 0.75rem; min-width: 0; padding: 0.5rem 0.75rem; border: 1px solid var(--border-color); background: var(--bg-secondary); border-radius: 8px; }
  .task-poster { width: 40px; height: 56px; flex-shrink: 0; border-radius: 4px; object-fit: contain; background: var(--bg-tertiary); }
  .placeholder { display: flex; align-items: center; justify-content: center; color: var(--text-muted); }
  .placeholder svg { width: 22px; height: 22px; }
  .task-content { flex: 1; min-width: 0; }
  .task-title { font-size: 0.85rem; font-weight: 500; color: var(--text-primary); white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
  .task-episode { font-size: 0.72rem; color: var(--text-muted); margin-top: 0.15rem; }
  .task-progress { display: flex; align-items: center; gap: 0.5rem; margin-top: 0.3rem; font-size: 0.75rem; font-variant-numeric: tabular-nums; }
  progress { appearance: none; flex: 1; width: 100%; min-width: 0; height: 6px; border: 0; border-radius: 4px; overflow: hidden; background: var(--border-color); accent-color: var(--accent); }
  progress::-webkit-progress-bar { background: var(--border-color); }
  progress::-webkit-progress-value { background: var(--accent); border-radius: 4px; }
  progress::-moz-progress-bar { background: var(--accent); border-radius: 4px; }
  .phase-converting progress { accent-color: var(--warning); }
  .phase-converting progress::-webkit-progress-value { background: var(--warning); }
  .phase-converting progress::-moz-progress-bar { background: var(--warning); }
  .task-detail { margin: 0.2rem 0 0; font-size: 0.72rem; color: var(--text-muted); overflow-wrap: anywhere; }
  .ready { color: var(--success); }
  .phase-error .task-detail, .phase-interrupted .task-detail { color: var(--danger); }
  .series-row {
    display: flex; align-items: center; gap: 0.75rem;
    background: var(--bg-secondary); border-radius: 8px; padding: 0.5rem 0.75rem;
    border: 1px solid var(--border-color);
    animation: motion-rise var(--motion-enter) var(--motion-ease-out) backwards;
    animation-delay: min(calc(var(--i, 0) * var(--motion-stagger)), var(--motion-stagger-max));
  }
  .series-row.active { border-color: var(--accent-bg); }
  .series-row.analysing { opacity: 0.65; }
  .series-row.done { opacity: 0.7; }
  .series-row.skipped { opacity: 0.5; }
  .series-row.waiting { opacity: 0.4; }

  .poster-thumb {
    width: 40px; height: 56px; border-radius: 4px; object-fit: contain;
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
    height: 100%; background: var(--accent); border-radius: 4px; transition: width var(--motion-enter) var(--motion-ease-out);
  }
  .pbar-text { font-size: 0.75rem; color: var(--accent-light); width: 2.5rem; text-align: right; }


  @media (max-width: 768px) {
    .task-card { padding: 0.4rem 0.5rem; gap: 0.5rem; }
    .task-poster { width: 32px; height: 44px; }
    .task-title { font-size: 0.8rem; }
    .series-row { padding: 0.4rem 0.5rem; gap: 0.5rem; flex-wrap: nowrap; }
    .poster-thumb { width: 32px; height: 44px; }
    .pbar-wrap { min-width: 100px; }
    .pbar-text { width: 2rem; font-size: 0.7rem; }
    .series-name { font-size: 0.8rem; }
    .series-status { font-size: 0.7rem; }
  }
</style>
