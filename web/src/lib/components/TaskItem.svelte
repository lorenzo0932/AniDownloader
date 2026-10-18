<script>
  // Riga singola della lista progressi (analisi/attiva/completata/saltata/attesa).
  // La variante determina markup e stile; task (per le varianti di attività) o
  // series (per analisi/attesa) portano i dati.
  let { variant = 'active', task = null, series = null, poster = '', index = 0 } = $props();
</script>

{#if variant === 'phase' && task}
  <article class="task-card phase-{task.phase}">
    <div class="task-title">{task.seriesName}</div>
    <div class="task-episode">{task.episode > 0 ? `Episodio ${task.episode}` : 'Serie'}</div>
    {#if ['downloading', 'converting'].includes(task.phase) && task.percent >= 0}
      <div class="task-progress">
        <progress max="100" value={task.percent} aria-label={`${task.seriesName}: ${task.phase === 'downloading' ? 'download' : 'conversione'}`}></progress>
        <span>{Math.round(task.percent)}%</span>
      </div>
    {/if}
    {#if task.phase === 'waiting_conversion'}
      <p class="task-detail ready">✓ Download completo</p>
    {:else if task.phase === 'completed'}
      <p class="task-detail ready">✓ Pronto</p>
    {:else if task.detail}
      <p class="task-detail">{task.detail}</p>
    {/if}
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
  .task-card { min-width: 0; padding: 0.8rem; border: 1px solid var(--border-color); background: var(--bg-secondary); border-radius: 8px; }
  .task-title { font-size: 0.9rem; font-weight: 600; color: var(--text-primary); overflow-wrap: anywhere; }
  .task-episode { font-size: 0.78rem; color: var(--text-muted); margin-top: 0.15rem; }
  .task-progress { display: flex; align-items: center; gap: 0.65rem; margin-top: 0.65rem; font-size: 0.8rem; font-variant-numeric: tabular-nums; }
  progress { appearance: none; width: 100%; min-width: 0; height: 8px; border: 0; border-radius: 4px; overflow: hidden; background: var(--border-color); accent-color: var(--accent); }
  progress::-webkit-progress-bar { background: var(--border-color); }
  progress::-webkit-progress-value { background: var(--accent); border-radius: 4px; }
  progress::-moz-progress-bar { background: var(--accent); border-radius: 4px; }
  .phase-converting progress { accent-color: var(--warning); }
  .phase-converting progress::-webkit-progress-value { background: var(--warning); }
  .phase-converting progress::-moz-progress-bar { background: var(--warning); }
  .task-detail { margin: 0.4rem 0 0; font-size: 0.76rem; color: var(--text-muted); overflow-wrap: anywhere; }
  .ready { color: var(--success); }
  .phase-error { border-color: var(--danger); }
  .phase-error .task-detail, .phase-interrupted .task-detail { color: var(--danger); }
  .phase-waiting_conversion, .phase-completed, .phase-skipped, .phase-queued { padding: 0.6rem 0.8rem; }
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
    .series-row { padding: 0.4rem 0.5rem; gap: 0.5rem; flex-wrap: nowrap; }
    .poster-thumb { width: 32px; height: 44px; }
    .pbar-wrap { min-width: 100px; }
    .pbar-text { width: 2rem; font-size: 0.7rem; }
    .series-name { font-size: 0.8rem; }
    .series-status { font-size: 0.7rem; }
  }
</style>
