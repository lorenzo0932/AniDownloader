<script>
  // Card dell'ultimo avvio: un solo oggetto, una sola CTA.
  // Coda e problemi globali vivono nella strip sotto la card.
  let { run = null, tasks = [], onopen = null } = $props();

  const ACTIVE_PHASES = Object.freeze([
    'downloading', 'verifying_download', 'waiting_conversion', 'converting', 'finalizing',
  ]);

  const PHASE_LABELS = Object.freeze({
    queued: 'In coda',
    queued_download: 'In coda',
    retry_wait: 'In attesa',
    downloading: 'In download',
    verifying_download: 'Verifica integrità',
    waiting_conversion: 'In attesa di conversione',
    downloaded_waiting_conversion: 'In attesa di conversione',
    converting: 'In conversione',
    finalizing: 'Finalizzazione',
  });

  const STATE_LABELS = Object.freeze({
    in_corso: 'In corso',
    completato: 'Completato',
    completato_con_errori: 'Completato con errori',
    interrotto: 'Interrotto',
  });

  let inCorso = $derived(run?.stato === 'in_corso');
  // Task live (SSE/download status): gli episodi attivi hanno il percent reale
  // di download/conversione, non solo il conteggio dei completati.
  let activeTasks = $derived(
    (tasks || []).filter(t => Number(t.episode) > 0 && ACTIVE_PHASES.includes(t.phase))
  );
  let progressTasks = $derived(
    activeTasks.filter(t => Number.isFinite(Number(t.percent)) && Number(t.percent) >= 0)
  );
  let percent = $derived.by(() => {
    const totale = Number(run?.totale) || 0;
    const base = Math.min(100, Math.max(0, Math.round(Number(run?.percent) || 0)));
    if (!inCorso || totale <= 0 || progressTasks.length === 0) return base;
    // completati + frazione reale degli episodi attivi, sul totale del run.
    const completati = Math.max(0, Number(run?.completati) || 0);
    const live = progressTasks.reduce(
      (sum, t) => sum + Math.min(100, Math.max(0, Number(t.percent))) / 100,
      0
    );
    return Math.min(100, Math.max(0, Math.round(((completati + live) / totale) * 100)));
  });
  let statoLabel = $derived(STATE_LABELS[run?.stato] || run?.stato || '');
  let origineLabel = $derived(run?.origine === 'automatico' ? 'automatico' : 'manuale');
  let faseCorrente = $derived(
    run?.corrente ? (PHASE_LABELS[run.corrente.fase] || run.corrente.fase || 'In corso') : ''
  );
  let whenLabel = $derived.by(() => {
    if (!run) return '';
    const parts = [formatWhen(run.avviato_il)];
    const totale = Number(run.totale) || 0;
    if (totale > 0) parts.push(`${totale} ${totale === 1 ? 'episodio' : 'episodi'}`);
    const durata = formatDuration(run.avviato_il, inCorso ? new Date().toISOString() : run.concluso_il);
    if (durata) parts.push(durata);
    return parts.filter(Boolean).join(' · ');
  });

  function formatWhen(value) {
    if (!value) return '';
    const date = new Date(value);
    if (Number.isNaN(date.getTime())) return '';
    const now = new Date();
    const sameDay = (a, b) =>
      a.getFullYear() === b.getFullYear() &&
      a.getMonth() === b.getMonth() &&
      a.getDate() === b.getDate();
    const yesterday = new Date(now);
    yesterday.setDate(now.getDate() - 1);
    const time = new Intl.DateTimeFormat('it-IT', {
      hour: '2-digit', minute: '2-digit',
    }).format(date);
    if (sameDay(date, now)) return `Oggi, ${time}`;
    if (sameDay(date, yesterday)) return `Ieri, ${time}`;
    if (date.getFullYear() !== now.getFullYear()) {
      return new Intl.DateTimeFormat('it-IT', {
        dateStyle: 'short', timeStyle: 'short',
      }).format(date);
    }
    const day = new Intl.DateTimeFormat('it-IT', {
      day: '2-digit', month: '2-digit',
    }).format(date);
    return `${day}, ${time}`;
  }

  function formatDuration(start, end) {
    if (!start || !end) return '';
    const from = new Date(start).getTime();
    const to = new Date(end).getTime();
    if (!Number.isFinite(from) || !Number.isFinite(to) || to < from) return '';
    const total = Math.round((to - from) / 1000);
    if (total < 60) return `${total} s`;
    const minutes = Math.floor(total / 60);
    const seconds = total % 60;
    if (minutes < 60) return seconds ? `${minutes} min ${seconds} s` : `${minutes} min`;
    return `${Math.floor(minutes / 60)} h ${minutes % 60} min`;
  }

  function handleOpen(event) {
    event?.preventDefault();
    onopen?.(event);
  }
</script>

<div class="run-card">
  {#if !run}
    <p class="run-empty" aria-live="polite">
      Nessun avvio registrato. Avvia un download o attendi il prossimo controllo automatico.
    </p>
  {:else}
    <div class="run-head">
      <h2 class="run-title" aria-live="polite">{inCorso ? 'Avvio corrente' : 'Ultimo avvio'}</h2>
      <span class="origin-badge origin-{origineLabel}">{origineLabel}</span>
      <span class="run-state state-{run.stato}">{statoLabel}</span>
    </div>

    {#if whenLabel}
      <p class="run-when">{whenLabel}</p>
    {/if}

    {#if inCorso && (run.totale ?? 0) > 0}
      <div
        class="run-progress"
        role="progressbar"
        aria-valuemin="0"
        aria-valuemax="100"
        aria-valuenow={percent}
        aria-valuetext="{run.completati} di {run.totale} episodi"
      >
        <span class="run-progress-fill" style={`width: ${percent}%`}></span>
      </div>
    {/if}

    {#if inCorso && activeTasks.length > 0}
      <ul class="run-tasks" aria-label="Episodi in corso">
        {#each activeTasks as t (t.key ?? `${t.seriesName}-${t.episode}`)}
          {@const pct = Math.min(100, Math.max(0, Math.round(Number(t.percent) || 0)))}
          {@const hasPct = Number.isFinite(Number(t.percent)) && Number(t.percent) >= 0}
          <li class="run-task">
            <div class="task-line">
              <span class="task-name" title={t.seriesName}>{t.seriesName}</span>
              <span class="task-ep">Ep. {t.episode}</span>
              <span class="task-phase">{PHASE_LABELS[t.phase] || t.phase || 'In corso'}</span>
              {#if hasPct}<span class="task-pct">{pct}%</span>{/if}
            </div>
            {#if hasPct}
              <div class="task-bar" role="progressbar"
                   aria-label={`${t.seriesName} Ep. ${t.episode}`}
                   aria-valuemin="0" aria-valuemax="100" aria-valuenow={pct}>
                <span class="task-bar-fill" style={`width: ${pct}%`}></span>
              </div>
            {/if}
          </li>
        {/each}
      </ul>
    {:else if inCorso && run.corrente}
      <p class="run-current">
        {run.corrente.serie} · Ep. {run.corrente.episodio} · {faseCorrente}
      </p>
    {/if}

    {#if (run.totale ?? 0) === 0}
      <p class="run-outcome">
        <span class="outcome-muted">Nessun episodio nuovo</span>
        {#if (run.saltati ?? 0) > 0}
          <span class="outcome-sep" aria-hidden="true">·</span>
          <span class="count-skipped">
            {run.saltati} {run.saltati === 1 ? 'serie già aggiornata' : 'serie già aggiornate'}
          </span>
        {/if}
      </p>
    {:else}
      <p class="run-outcome">
        <span class="count-done">
          {run.completati} {run.completati === 1 ? 'completato' : 'completati'}
        </span>
        {#if inCorso || (run.in_corso ?? 0) > 0}
          <span class="outcome-sep" aria-hidden="true">·</span>
          <span class="count-active">{run.in_corso} in corso</span>
        {/if}
        {#if (run.non_riusciti ?? 0) > 0}
          <span class="outcome-sep" aria-hidden="true">·</span>
          <span class="count-error">
            {run.non_riusciti} {run.non_riusciti === 1 ? 'non riuscito' : 'non riusciti'}
          </span>
        {/if}
        {#if (run.saltati ?? 0) > 0}
          <span class="outcome-sep" aria-hidden="true">·</span>
          <span class="count-skipped">
            {run.saltati} {run.saltati === 1 ? 'serie già aggiornata' : 'serie già aggiornate'}
          </span>
        {/if}
      </p>
    {/if}

    <a class="run-cta" href="/attivita" onclick={handleOpen}>
      {inCorso ? 'Apri attività' : 'Vedi dettaglio'}
    </a>
  {/if}
</div>

<style>
  .run-card {
    display: flex; flex-direction: column; gap: 0.35rem;
    background: var(--bg-secondary); border: 1px solid var(--border-color); border-radius: 8px;
    padding: 0.7rem 0.9rem; font-size: 0.85rem; color: var(--text-secondary);
  }

  .run-empty { margin: 0; color: var(--text-muted); font-size: 0.85rem; }

  .run-head {
    display: flex; flex-wrap: wrap; align-items: center;
    gap: 0.25rem 0.5rem; min-width: 0;
  }
  .run-title { margin: 0; font-size: 0.9rem; font-weight: 600; color: var(--text-primary); }

  .origin-badge {
    border: 1px solid var(--border-color); border-radius: 999px;
    padding: 0.1rem 0.45rem; font-size: 0.72rem; font-weight: 600;
    color: var(--text-secondary); background: var(--bg-tertiary);
    white-space: nowrap;
  }
  .origin-automatico { color: var(--text-muted); background: transparent; }

  .run-state {
    margin-left: auto; font-size: 0.72rem; font-weight: 600;
    padding: 0.1rem 0.45rem; border-radius: 999px;
    background: var(--bg-tertiary); color: var(--text-muted);
    white-space: nowrap;
  }
  .run-state.state-in_corso { background: var(--accent-bg); color: var(--accent-light); }
  .run-state.state-completato { color: var(--success); }
  .run-state.state-completato_con_errori,
  .run-state.state-interrotto { background: var(--danger-bg); color: var(--danger); }

  .run-when {
    margin: 0; color: var(--text-secondary); font-size: 0.8rem;
    font-variant-numeric: tabular-nums;
  }

  .run-progress {
    height: 6px; border-radius: 3px; background: var(--bg-tertiary);
    overflow: hidden; margin: 0.15rem 0;
  }
  .run-progress-fill {
    display: block; height: 100%; background: var(--accent);
    transition: width 220ms var(--motion-ease-out);
  }

  .run-current { margin: 0; font-size: 0.85rem; font-weight: 500; color: var(--text-primary); }

  .run-tasks {
    list-style: none; margin: 0.15rem 0 0; padding: 0;
    display: flex; flex-direction: column; gap: 0.45rem;
  }
  .run-task { display: flex; flex-direction: column; gap: 0.18rem; min-width: 0; }
  .task-line { display: flex; align-items: baseline; gap: 0.35rem; min-width: 0; }
  .task-name {
    flex: 1; min-width: 0; font-size: 0.82rem; font-weight: 500; color: var(--text-primary);
    overflow: hidden; text-overflow: ellipsis; white-space: nowrap;
  }
  .task-ep, .task-phase { font-size: 0.75rem; white-space: nowrap; }
  .task-ep { color: var(--text-secondary); }
  .task-phase { color: var(--text-muted); }
  .task-pct {
    margin-left: auto; font-size: 0.75rem; font-weight: 600;
    color: var(--accent-light); font-variant-numeric: tabular-nums;
  }
  .task-bar { height: 4px; border-radius: 2px; background: var(--bg-tertiary); overflow: hidden; }
  .task-bar-fill {
    display: block; height: 100%; background: var(--accent);
    transition: width 220ms var(--motion-ease-out);
  }

  .run-outcome {
    margin: 0; display: flex; flex-wrap: wrap; align-items: center;
    gap: 0.3rem 0.45rem; font-size: 0.82rem; color: var(--text-secondary);
  }
  .outcome-muted { color: var(--text-secondary); }
  .outcome-sep { color: var(--text-muted); }
  .count-done { color: var(--success); font-weight: 600; }
  .count-active { color: var(--text-secondary); font-weight: 600; }
  .count-error { color: var(--danger); font-weight: 600; }
  .count-skipped { color: var(--warning); font-weight: 600; }

  .run-cta {
    align-self: flex-start; color: var(--accent-light); font-weight: 600;
    text-decoration: none; font-size: 0.82rem; margin-top: 0.1rem;
  }
  .run-cta:hover { text-decoration: underline; }

  @media (any-pointer: coarse) {
    .run-cta { display: inline-flex; align-items: center; min-height: 44px; }
  }
</style>
