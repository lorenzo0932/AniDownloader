<script>
  // Riga di un avvio (run): origine, ora relativa, conteggi, progressbar e
  // espansione nei suoi episodi. "Riprova tutti" è un controllo separato dal
  // bottone di espansione (niente button annidati).
  import JobRow from './JobRow.svelte';

  let {
    run,
    expanded = false,
    loading = false,
    jobs = [],
    error = '',
    onToggle,
    onRetryFailed,
    onRetryJob,
    onReload,
  } = $props();

  let jobsAperti = $state({});

  const STATI = {
    in_corso: 'In corso',
    completato: 'Completato',
    completato_con_errori: 'Con errori',
    interrotto: 'Interrotto',
  };

  let origine = $derived(
    run.origine === 'manuale' || run.origine === 'interactive' ? 'manuale' : 'automatico'
  );
  let titolo = $derived(`Avvio ${origine}`);
  let stato = $derived(STATI[run.stato] || run.stato || '');
  let durata = $derived(formattaDurata(run));
  let percent = $derived(Math.max(0, Math.min(100, Number(run.percent) || 0)));
  let hasCounts = $derived(
    (run.totale ?? 0) > 0 || (run.non_riusciti ?? 0) > 0 || (run.saltati ?? 0) > 0
  );

  function formattaDurata(r) {
    if (!r.concluso_il || !r.avviato_il) return '';
    const fine = Date.parse(r.concluso_il);
    const inizio = Date.parse(r.avviato_il);
    if (!Number.isFinite(fine) || !Number.isFinite(inizio) || fine < inizio) return '';
    const sec = Math.round((fine - inizio) / 1000);
    if (sec < 60) return `${sec}s`;
    const minuti = Math.floor(sec / 60);
    const secondi = sec % 60;
    if (minuti < 60) return `${minuti}m ${secondi}s`;
    const ore = Math.floor(minuti / 60);
    return `${ore}h ${minuti % 60}m`;
  }

  function tempoRelativo(iso) {
    if (!iso) return '';
    const t = Date.parse(iso);
    if (Number.isNaN(t)) return '';
    const sec = Math.max(0, Math.floor((Date.now() - t) / 1000));
    if (sec < 60) return 'adesso';
    if (sec < 3600) return `${Math.floor(sec / 60)} min fa`;
    if (sec < 86400) return `${Math.floor(sec / 3600)} h fa`;
    return `${Math.floor(sec / 86400)} g fa`;
  }

  function toggleJob(id) {
    jobsAperti = { ...jobsAperti, [id]: !jobsAperti[id] };
  }
</script>

<li class="job run" data-run-id={run.id}>
  <button class="run-main" onclick={() => onToggle?.(run)}
          aria-expanded={expanded ? 'true' : 'false'}
          aria-controls={`run-jobs-${run.id}`}>
    <span class="run-top">
      <span class="run-badge origine-{origine}">{origine}</span>
      <span class="run-title">{titolo}</span>
      <span class="run-stato stato-{run.stato}">{stato}</span>
      <span class="chevron" aria-hidden="true" class:aperto={expanded}>▾</span>
    </span>
    <span class="run-meta">
      <span>{tempoRelativo(run.avviato_il)}</span>
      <span class="run-sep" aria-hidden="true">·</span>
      <span>{(run.totale ?? 0) === 0 ? 'Nessun episodio' : `${run.totale} episodi`}</span>
      {#if hasCounts}
        <span class="run-sep" aria-hidden="true">·</span>
        <span class="run-counts">
          {#if (run.totale ?? 0) > 0}
            <span class="ok">✓ {run.completati ?? 0}</span>
            <span class="corso">◷ {run.in_corso ?? 0}</span>
          {/if}
          {#if (run.non_riusciti ?? 0) > 0}
            <span class="ko">! {run.non_riusciti}</span>
          {/if}
          {#if (run.saltati ?? 0) > 0}
            <span class="skip">{run.saltati} {run.saltati === 1 ? 'saltato' : 'saltati'}</span>
          {/if}
        </span>
      {/if}
      {#if durata}
        <span class="run-sep" aria-hidden="true">·</span>
        <span>{durata}</span>
      {/if}
    </span>
  </button>

  {#if run.stato === 'in_corso' && (run.totale ?? 0) > 0}
    <div class="run-progress" role="progressbar" aria-valuemin="0"
         aria-valuemax="100" aria-valuenow={percent}
         aria-valuetext={`${run.completati ?? 0} di ${run.totale ?? 0} episodi`}>
      <div class="run-bar" style={`width:${percent}%`}></div>
    </div>
  {/if}

  {#if expanded}
    <div class="run-jobs" id={`run-jobs-${run.id}`}>
      {#if (run.non_riusciti ?? 0) > 0}
        <div class="run-actions">
          <button class="btn-retry-all" onclick={() => onRetryFailed?.(run)}
                  aria-label={`Riprova tutti i non riusciti di ${titolo}`}>
            Riprova tutti i non riusciti
          </button>
        </div>
      {/if}
      {#if loading}
        <div class="skeleton" aria-label="Caricamento episodi">
          <span class="skeleton-row"></span>
          <span class="skeleton-row"></span>
          <span class="skeleton-row"></span>
        </div>
      {:else if error}
        <div class="run-error" role="alert">
          <span>{error}</span>
          <button class="btn-retry" onclick={() => onReload?.(run)}>Riprova</button>
        </div>
      {:else if jobs.length === 0}
        <p class="run-empty">Nessun episodio in questo avvio.</p>
      {:else}
        <ul class="job-list">
          {#each jobs as job (job.id)}
            <JobRow {job} expanded={!!jobsAperti[job.id]}
                    onToggle={() => toggleJob(job.id)}
                    onRetry={onRetryJob} />
          {/each}
        </ul>
      {/if}
    </div>
  {/if}
</li>

<style>
  .job {
    animation: motion-rise var(--motion-enter) var(--motion-ease-out) backwards;
    background: var(--bg-secondary); border: 1px solid var(--border-color);
    border-radius: 10px; overflow: hidden;
  }
  .run-main {
    width: 100%; display: flex; flex-direction: column; align-items: stretch; gap: 0.3rem;
    background: none; border: none; color: var(--text-primary);
    padding: 0.65rem 0.8rem; text-align: left; cursor: pointer; font-size: 0.9rem;
  }
  .run-main:hover { background: var(--bg-hover); }
  .run-main:focus-visible, .btn-retry-all:focus-visible, .btn-retry:focus-visible {
    outline: 2px solid var(--accent); outline-offset: -2px;
  }
  .run-top { display: flex; align-items: center; gap: 0.5rem; min-width: 0; }
  .run-badge {
    font-size: 11px; padding: 0.1rem 0.45rem; border-radius: 999px;
    background: var(--bg-tertiary); color: var(--text-secondary);
    border: 1px solid var(--border-color); white-space: nowrap;
  }
  .run-badge.origine-manuale { color: var(--accent-light); border-color: var(--accent); }
  .run-title {
    flex: 1; min-width: 0; font-weight: 600;
    white-space: nowrap; overflow: hidden; text-overflow: ellipsis;
  }
  .run-meta {
    display: flex; flex-wrap: wrap; align-items: center; gap: 0.2rem 0.45rem;
    color: var(--text-muted); font-size: 12px;
  }
  .run-sep { color: var(--text-muted); }
  .run-counts { display: inline-flex; gap: 0.25rem; white-space: nowrap; }
  .run-counts > span + span::before {
    content: '·'; color: var(--text-muted); margin-right: 0.25rem;
  }
  .run-counts .ok { color: var(--success); }
  .run-counts .corso { color: var(--accent-light); }
  .run-counts .ko { color: var(--danger); }
  .run-counts .skip { color: var(--warning); }
  .run-stato {
    margin-left: auto; font-size: 11px; font-weight: 600;
    padding: 0.1rem 0.45rem; border-radius: 999px;
    background: var(--bg-tertiary); color: var(--text-muted);
    white-space: nowrap;
  }
  .run-stato.stato-in_corso {
    background: var(--accent-bg); color: var(--accent-light);
    animation: motion-ring calc(var(--motion-shimmer) * 2.5) var(--motion-ease-out) infinite;
  }
  .run-stato.stato-completato { color: var(--success); }
  .run-stato.stato-completato_con_errori,
  .run-stato.stato-interrotto { background: var(--danger-bg); color: var(--danger); }
  .chevron { color: var(--text-muted); font-size: 12px; }
  .chevron.aperto { transform: rotate(180deg); }

  .run-actions { display: flex; justify-content: flex-end; padding: 0.5rem 0.5rem 0; }
  .btn-retry-all {
    background: none; border: 1px solid var(--border-color);
    color: var(--accent-light); border-radius: 6px;
    padding: 0.35rem 0.7rem; cursor: pointer; font-size: 12px;
    white-space: nowrap;
  }
  .btn-retry-all:hover { background: var(--bg-hover); }

  .run-progress {
    height: 4px; margin: 0 0.8rem 0.65rem;
    background: var(--bg-tertiary); border-radius: 2px;
  }
  /* Barra in corso: riflesso che scorre + punta luminosa che avanza con la
     width. Il glow esce dalla traccia (niente overflow hidden) senza
     occupare spazio nel layout. */
  .run-bar {
    position: relative; height: 100%; border-radius: 2px;
    background-color: var(--accent);
    background-image: linear-gradient(90deg,
      transparent 30%, rgba(255, 255, 255, 0.42) 50%, transparent 70%);
    background-size: 200% 100%;
    background-repeat: no-repeat;
    box-shadow: 0 0 6px -2px var(--accent-light);
    animation: motion-shimmer calc(var(--motion-shimmer) * 2) linear infinite;
    transition: width var(--motion-fast) var(--motion-ease-out);
  }
  .run-bar::after {
    content: '';
    position: absolute; top: 50%; right: 0;
    width: 4px; height: 4px; border-radius: 50%;
    transform: translate(50%, -50%);
    background: var(--accent-light);
    box-shadow: 0 0 6px 1px var(--accent-light);
    animation: motion-glow calc(var(--motion-shimmer) * 1.5) var(--motion-ease-in-out) infinite;
  }

  .run-jobs { border-top: 1px solid var(--border-color); padding: 0.5rem; }
  .job-list { list-style: none; display: flex; flex-direction: column; gap: 0.35rem; }
  .run-empty { color: var(--text-muted); font-size: 12px; margin: 0.35rem 0.5rem; }
  .run-error {
    display: flex; align-items: center; justify-content: space-between;
    gap: 0.75rem; padding: 0.5rem; color: var(--danger); font-size: 12px;
  }
  .run-error .btn-retry {
    background: none; border: 1px solid var(--border-color);
    color: var(--accent-light); border-radius: 6px;
    padding: 0.35rem 0.7rem; cursor: pointer; font-size: 12px;
  }
  .run-error .btn-retry:hover { background: var(--bg-hover); }

  .skeleton { display: flex; flex-direction: column; gap: 0.4rem; padding: 0.35rem; }
  .skeleton-row {
    display: block; height: 42px; border-radius: 8px;
    background: linear-gradient(90deg, var(--bg-tertiary), var(--bg-hover), var(--bg-tertiary));
    background-size: 200% 100%;
    animation: motion-shimmer 1.2s infinite linear;
  }

  @media (max-width: 768px) {
    .run-main { padding: 0.6rem; }
    .run-meta { gap: 0.15rem 0.35rem; }
  }
</style>
