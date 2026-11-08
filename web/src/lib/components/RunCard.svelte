<script>
  // Card unica di avvio: aggrega l'ultimo run (manuale o automatico) e la
  // coda complessiva. Il dettaglio per episodio vive in Attività.
  let { run = null, coda = null, problemi = null, onopen = null } = $props();

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
  let percent = $derived(
    Math.min(100, Math.max(0, Math.round(Number(run?.percent) || 0)))
  );
  let statoLabel = $derived(STATE_LABELS[run?.stato] || run?.stato || '');
  let origineLabel = $derived(run?.origine === 'automatico' ? 'automatico' : 'manuale');
  let avvioLabel = $derived(formatMoment(run?.avviato_il));
  let conclusioneLabel = $derived(formatMoment(run?.concluso_il));
  let durata = $derived(inCorso ? '' : formatDuration(run?.avviato_il, run?.concluso_il));
  let faseCorrente = $derived(
    run?.corrente ? (PHASE_LABELS[run.corrente.fase] || run.corrente.fase || 'In corso') : ''
  );
  let codaItems = $derived.by(() => {
    if (!inCorso || !coda) return [];
    const items = [];
    if (coda.queued > 0) items.push(`${coda.queued} in attesa`);
    if (coda.downloading > 0) items.push(`${coda.downloading} in download`);
    if (coda.converting > 0) items.push(`${coda.converting} in conversione`);
    if (coda.retry > 0) items.push(`${coda.retry} in retry`);
    return items;
  });
  let problemiTotali = $derived(
    (problemi?.non_riusciti || 0) + (problemi?.interrotti || 0)
  );

  function formatMoment(value) {
    if (!value) return '';
    const date = new Date(value);
    if (Number.isNaN(date.getTime())) return '';
    return new Intl.DateTimeFormat('it-IT', {
      dateStyle: 'short', timeStyle: 'short',
    }).format(date);
  }

  function formatDuration(start, end) {
    if (!start || !end) return '';
    const from = new Date(start).getTime();
    const to = new Date(end).getTime();
    if (!Number.isFinite(from) || !Number.isFinite(to) || to < from) return '';
    const total = Math.round((to - from) / 1000);
    const hours = Math.floor(total / 3600);
    const minutes = Math.floor((total % 3600) / 60);
    const seconds = total % 60;
    if (hours > 0) return `${hours} h ${minutes} min`;
    if (minutes > 0) return `${minutes} min ${seconds} s`;
    return `${seconds} s`;
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
      <h2 class="run-title" aria-live="polite">{inCorso ? 'Avvio in corso' : 'Ultimo avvio'}</h2>
      <span class="origin-badge origin-{origineLabel}">{origineLabel}</span>
      <span class="run-state state-{run.stato}">{statoLabel}</span>
    </div>

    {#if avvioLabel || conclusioneLabel || durata}
      <div class="run-meta">
        {#if avvioLabel}<span>Avviato {avvioLabel}</span>{/if}
        {#if !inCorso && conclusioneLabel}<span>Concluso {conclusioneLabel}</span>{/if}
        {#if durata}<span>Durata {durata}</span>{/if}
      </div>
    {/if}

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

    {#if inCorso && run.corrente}
      <p class="run-current">
        {run.corrente.serie} · Ep. {run.corrente.episodio} · {faseCorrente}
      </p>
    {/if}

    <div class="run-counts">
      <span class="count count-done">
        {run.completati} {run.completati === 1 ? 'completato' : 'completati'}
      </span>
      <span class="count count-active">{run.in_corso} in corso</span>
      <span class="count count-error">
        {run.non_riusciti} {run.non_riusciti === 1 ? 'non riuscito' : 'non riusciti'}
      </span>
      {#if (run.saltati || 0) > 0}
        <span class="count count-skipped">
          {run.saltati} {run.saltati === 1 ? 'saltato' : 'saltati'}
        </span>
      {/if}
      <span class="count count-total">
        {run.totale} {run.totale === 1 ? 'totale' : 'totali'}
      </span>
    </div>

    {#if inCorso && codaItems.length > 0}
      <p class="run-queue">Coda: {codaItems.join(' · ')}</p>
    {/if}

    {#if !inCorso && problemiTotali > 0}
      <p class="run-problems">Problemi registrati: {problemiTotali}</p>
    {/if}

    <a class="run-cta" href="/attivita" onclick={handleOpen}>
      {inCorso ? 'Apri attività' : 'Vedi dettaglio'}
    </a>
  {/if}
</div>

<style>
  .run-card {
    display: flex; flex-direction: column; gap: 0.4rem;
    background: var(--bg-secondary); border: 1px solid var(--border-color); border-radius: 8px;
    padding: 0.65rem 0.75rem; font-size: 0.85rem; color: var(--text-secondary);
  }

  .run-empty { margin: 0; color: var(--text-muted); font-size: 0.85rem; }

  .run-head { display: flex; flex-wrap: wrap; align-items: center; gap: 0.4rem 0.6rem; }
  .run-title { margin: 0; font-size: 0.85rem; font-weight: 600; color: var(--text-primary); }

  .origin-badge {
    border: 1px solid var(--border-color); border-radius: 4px;
    padding: 0.1rem 0.4rem; font-size: 0.72rem; font-weight: 600;
    letter-spacing: 0.03em; text-transform: uppercase;
  }
  .origin-manuale { color: var(--text-secondary); background: var(--bg-tertiary); }
  .origin-automatico { color: var(--text-muted); background: transparent; }

  .run-state { margin-left: auto; font-size: 0.75rem; font-weight: 600; color: var(--text-muted); }
  .run-state.state-completato { color: var(--success); }
  .run-state.state-completato_con_errori, .run-state.state-interrotto { color: var(--danger); }

  .run-meta { display: flex; flex-wrap: wrap; gap: 0.25rem 0.75rem; font-size: 0.78rem; color: var(--text-muted); }

  .run-progress {
    height: 6px; border-radius: 3px; background: var(--bg-tertiary);
    overflow: hidden; margin: 0.1rem 0;
  }
  .run-progress-fill { display: block; height: 100%; background: var(--accent); }

  .run-current { margin: 0; font-size: 0.85rem; font-weight: 500; color: var(--text-primary); }

  .run-counts { display: flex; flex-wrap: wrap; gap: 0.35rem 0.75rem; }
  .count { font-size: 0.8rem; font-weight: 600; }
  .count-done { color: var(--success); }
  .count-active { color: var(--text-secondary); }
  .count-error { color: var(--danger); }
  .count-skipped { color: var(--warning); }
  .count-total { color: var(--text-muted); }

  .run-queue, .run-problems { margin: 0; font-size: 0.78rem; color: var(--text-muted); }
  .run-problems { color: var(--warning); }

  .run-cta {
    align-self: flex-start; color: var(--accent-light); font-weight: 600;
    text-decoration: none; font-size: 0.85rem;
  }
  .run-cta:hover { text-decoration: underline; }

  @media (any-pointer: coarse) {
    .run-cta { display: inline-flex; align-items: center; min-height: 44px; }
  }
</style>
