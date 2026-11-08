<script>
  import { onMount } from 'svelte';
  import { pageEnter } from '../motion.js';
  import { api } from '../api.js';
  import RunRow from './RunRow.svelte';
  import JobRow from './JobRow.svelte';

  const TABS = [['recenti', 'Recenti'], ['problemi', 'Problemi'], ['storico', 'Storico']];
  const ORIGINI = [['tutte', 'Tutte'], ['manuale', 'Manuale'], ['automatico', 'Automatico']];

  const LIMITE_AVVII = 20;
  const LIMITE_EPISODI = 50;
  const MAX_AVVII = 100;   // limite massimo di /api/runs
  const MAX_EPISODI = 500; // limite massimo di /api/pipeline
  const MAX_RUN_POLL = 3;  // run aperti aggiornati a ogni polling
  const INTERVALLO_POLL = 5000;

  let vista = $state('avvii');       // avvii | episodi
  let tab = $state('recenti');       // recenti | problemi | storico
  let origine = $state('tutte');     // tutte | manuale | automatico
  let items = $state([]);
  let total = $state(0);
  let offset = $state(0);
  let hasMore = $state(false);
  let counts = $state({ in_corso: 0, recenti: 0, problemi: 0 });
  let loading = $state(false);
  let loadingMore = $state(false);
  let error = $state('');

  // Espansione episodi (vista Episodi) e run aperti (vista Avvii).
  let aperti = $state({});
  let runAperti = $state({});
  let runJobs = $state({});
  let runLoading = $state({});
  let runError = $state({});

  // Scarta le risposte di richieste superate (cambio tab/vista/origine).
  let sequenza = 0;

  function finestraTab() {
    if (tab === 'storico') return 'storico';
    if (tab === 'problemi') return 'tutti';
    return 'recenti';
  }

  async function load({ silent = false, more = false } = {}) {
    const seq = ++sequenza;
    const base = vista === 'avvii' ? LIMITE_AVVII : LIMITE_EPISODI;
    const cap = vista === 'avvii' ? MAX_AVVII : MAX_EPISODI;
    // Il polling silenzioso ricarica tutte le pagine già visibili, così non
    // riporta la lista alla prima pagina dopo un "Carica altri".
    const limite = more
      ? base
      : (silent ? Math.min(cap, Math.max(base, items.length)) : base);
    const da = more ? offset : 0;

    if (!silent && !more) loading = true;
    if (more) loadingMore = true;

    try {
      if (vista === 'avvii') {
        const data = await api.runs.list({
          window: finestraTab(),
          origine,
          stato: tab === 'problemi' ? 'problemi' : 'tutti',
          limit: limite,
          offset: da,
        });
        if (seq !== sequenza) return;
        const nuovi = data.items || [];
        items = more ? [...items, ...nuovi] : nuovi;
        total = Number.isInteger(data.total) ? data.total : items.length;
        counts = data.counts || counts;
        offset = da + nuovi.length;
        hasMore = items.length < total;
      } else {
        const opts = { window: finestraTab(), limit: limite, offset: da };
        if (tab === 'problemi') opts.fase = 'problemi';
        const data = await api.pipeline.list(opts);
        if (seq !== sequenza) return;
        const nuovi = data.items || [];
        items = more ? [...items, ...nuovi] : nuovi;
        if (Number.isInteger(data.total)) {
          total = data.total;
          hasMore = items.length < total;
        } else {
          total = da + nuovi.length;
          hasMore = nuovi.length === limite;
        }
        offset = da + nuovi.length;
      }
      error = '';
    } catch (e) {
      if (seq === sequenza) error = e.message || 'Errore di caricamento.';
    } finally {
      if (seq === sequenza) {
        loading = false;
        loadingMore = false;
      }
    }
  }

  function reset() {
    items = [];
    total = 0;
    offset = 0;
    hasMore = false;
    aperti = {};
    runAperti = {};
    runJobs = {};
    runLoading = {};
    runError = {};
    error = '';
  }

  function cambiaVista(v) {
    if (vista === v) return;
    vista = v;
    reset();
    load();
  }

  function scegliTab(t) {
    if (tab === t) return;
    tab = t;
    reset();
    load();
  }

  function scegliOrigine(o) {
    if (origine === o) return;
    origine = o;
    reset();
    load();
  }

  function toggle(id) {
    aperti = { ...aperti, [id]: !aperti[id] };
  }

  function toggleRun(id) {
    const aperto = !!runAperti[id];
    runAperti = { ...runAperti, [id]: !aperto };
    // La cache resta anche alla chiusura: alla riapertura non si rifetcha.
    if (!aperto) caricaRun(id);
  }

  async function caricaRun(id, forza = false, inserisci = false) {
    if (!forza && runJobs[id] !== undefined) return;
    runLoading = { ...runLoading, [id]: true };
    runError = { ...runError, [id]: '' };
    try {
      const data = await api.runs.get(id);
      runJobs = { ...runJobs, [id]: data.jobs || [] };
      if (data.run) {
        const esiste = items.some(r => r.id === id);
        if (esiste) items = items.map(r => (r.id === id ? data.run : r));
        else if (inserisci) items = [data.run, ...items];
      }
    } catch (e) {
      runError = { ...runError, [id]: e.message || 'Errore di caricamento.' };
    } finally {
      runLoading = { ...runLoading, [id]: false };
    }
  }

  async function riprova(job) {
    error = '';
    try {
      await api.pipeline.retry(job.id);
      await load({ silent: true });
      if (vista === 'avvii' && job.run_id) await caricaRun(job.run_id, true);
    } catch (e) {
      error = e.message || 'Retry non riuscito.';
    }
  }

  async function riprovaRun(run) {
    error = '';
    try {
      await api.runs.retryFailed(run.id);
      await load({ silent: true });
      await caricaRun(run.id, true);
    } catch (e) {
      error = e.message || 'Retry non riuscito.';
    }
  }

  // Polling dei run espansi: al massimo 3, in parallelo e senza toccare
  // lo stato di caricamento del pulsante Aggiorna.
  async function aggiornaRunAperti() {
    const ids = Object.keys(runAperti)
      .filter(id => runAperti[id])
      .slice(0, MAX_RUN_POLL);
    if (ids.length === 0) return;
    const esiti = await Promise.allSettled(ids.map(id => api.runs.get(id)));
    esiti.forEach((esito, i) => {
      if (esito.status !== 'fulfilled') return;
      const id = ids[i];
      const data = esito.value;
      runJobs = { ...runJobs, [id]: data.jobs || [] };
      if (data.run) items = items.map(r => (r.id === id ? data.run : r));
    });
  }

  async function poll() {
    if (typeof document !== 'undefined' && document.hidden) return;
    await load({ silent: true });
    if (vista === 'avvii') await aggiornaRunAperti();
  }

  async function apriDeepLink(id) {
    runAperti = { ...runAperti, [id]: true };
    await caricaRun(id, true, true);
  }

  let testoFinestra = $derived.by(() => {
    const cosa = vista === 'avvii' ? 'Avvii' : 'Episodi';
    if (tab === 'problemi') {
      return `${cosa} con errori o interruzioni, anche oltre 24 ore.`;
    }
    if (tab === 'storico') return `${cosa} conclusi da più di 24 ore.`;
    return `${cosa} in corso e conclusi nelle ultime 24 ore.`;
  });

  let testoVuoto = $derived.by(() => {
    if (vista === 'avvii') {
      if (tab === 'problemi') return 'Nessun avvio con problemi.';
      if (tab === 'storico') return 'Nessun avvio nello storico.';
      return 'Nessun avvio recente.';
    }
    if (tab === 'problemi') return 'Nessun episodio con problemi.';
    if (tab === 'storico') return 'Nessun episodio nello storico.';
    return 'Nessuna attività recente.';
  });

  onMount(() => {
    const avvia = async () => {
      const richiesto = new URLSearchParams(window.location.search).get('run');
      if (richiesto) {
        vista = 'avvii';
        tab = 'recenti';
      }
      await load();
      if (richiesto) await apriDeepLink(richiesto);
    };
    avvia();
    const timer = setInterval(poll, INTERVALLO_POLL);
    return () => clearInterval(timer);
  });
</script>

<div class="attivita" in:pageEnter|global>
  <header class="head">
    <h1>Attività</h1>
    <button class="btn-refresh" onclick={() => load()} disabled={loading}
            aria-label="Aggiorna attività">
      {loading ? 'Aggiorno…' : 'Aggiorna'}
    </button>
  </header>

  <div class="toolbar">
    <div class="viste" role="group" aria-label="Vista attività">
      <button class="vista-btn" aria-pressed={vista === 'avvii'}
              class:attivo={vista === 'avvii'}
              onclick={() => cambiaVista('avvii')}>Avvii</button>
      <button class="vista-btn" aria-pressed={vista === 'episodi'}
              class:attivo={vista === 'episodi'}
              onclick={() => cambiaVista('episodi')}>Episodi</button>
    </div>

    {#if vista === 'avvii'}
      <div class="origini" role="group" aria-label="Filtra per origine">
        {#each ORIGINI as [valore, label] (valore)}
          <button class="chip" aria-pressed={origine === valore}
                  class:attivo={origine === valore}
                  onclick={() => scegliOrigine(valore)}>{label}</button>
        {/each}
      </div>
    {/if}
  </div>

  <div class="tabs" role="tablist" aria-label="Attività">
    {#each TABS as [valore, label] (valore)}
      <button role="tab" id={`tab-${valore}`}
              aria-selected={tab === valore}
              aria-controls="panel-attivita"
              class:active={tab === valore}
              onclick={() => scegliTab(valore)}>
        {label}{#if vista === 'avvii' && valore === 'problemi' && counts.problemi > 0}{' '}({counts.problemi}){/if}
      </button>
    {/each}
  </div>

  <div id="panel-attivita" role="tabpanel" aria-labelledby={`tab-${tab}`}>
    <p class="finestra">{testoFinestra}</p>

    {#if error && items.length === 0}
      <div class="errore-stato" role="alert">
        <p>{error}</p>
        <button class="btn-retry" onclick={() => load()}>Riprova</button>
      </div>
    {:else}
      {#if error}
        <p class="err" role="alert">{error}</p>
      {/if}

      {#if loading && items.length === 0}
        <p class="empty">Caricamento…</p>
      {:else if items.length === 0}
        <p class="empty">{testoVuoto}</p>
      {:else if vista === 'avvii'}
        <ul class="run-list">
          {#each items as run, i (run.id)}
            <li class="run-item" style="--i:{Math.min(i, 8)}">
              <RunRow
                {run}
                expanded={!!runAperti[run.id]}
                loading={!!runLoading[run.id]}
                jobs={runJobs[run.id] || []}
                error={runError[run.id] || ''}
                onToggle={() => toggleRun(run.id)}
                onReload={() => caricaRun(run.id, true)}
                onRetryFailed={() => riprovaRun(run)}
                onRetryJob={riprova}
              />
            </li>
          {/each}
        </ul>
      {:else}
        <ul class="job-list">
          {#each items as job, i (job.id)}
            <li class="job-item" style="--i:{Math.min(i, 8)}">
              <JobRow {job} expanded={!!aperti[job.id]}
                      onToggle={() => toggle(job.id)}
                      onRetry={() => riprova(job)} />
            </li>
          {/each}
        </ul>
      {/if}

      {#if hasMore && items.length > 0}
        <button class="btn-altri" onclick={() => load({ more: true })}
                disabled={loadingMore}>
          {loadingMore ? 'Carico…' : 'Carica altri'}
        </button>
      {/if}
    {/if}
  </div>
</div>

<style>
  .attivita { padding: 1.5rem; max-width: 900px; margin: 0 auto; }
  .head { display: flex; align-items: center; justify-content: space-between; gap: 1rem; }
  .head h1 { font-size: 1.4rem; }
  .btn-refresh {
    background: var(--bg-tertiary); color: var(--text-primary);
    border: 1px solid var(--border-color); border-radius: 8px;
    padding: 0.45rem 0.8rem; cursor: pointer;
  }
  .btn-refresh:hover:not(:disabled) { background: var(--bg-hover); }
  .btn-refresh:disabled { color: var(--text-muted); cursor: default; }

  .toolbar {
    display: flex; flex-wrap: wrap; align-items: center;
    justify-content: space-between; gap: 0.5rem; margin: 1rem 0 0.5rem;
  }
  .viste { display: inline-flex; gap: 0.25rem; }
  .vista-btn {
    background: var(--bg-tertiary); color: var(--text-secondary);
    border: 1px solid var(--border-color); border-radius: 8px;
    padding: 0.35rem 0.8rem; cursor: pointer; font-size: 0.85rem;
  }
  .vista-btn:hover { background: var(--bg-hover); color: var(--text-primary); }
  .vista-btn.attivo {
    background: var(--accent-bg); color: var(--accent-light);
    border-color: var(--accent);
  }
  .origini { display: flex; flex-wrap: wrap; gap: 0.4rem; }

  .tabs {
    display: flex; gap: 0.25rem; margin: 0.5rem 0 0.75rem;
    border-bottom: 1px solid var(--border-color);
  }
  .tabs button {
    background: none; border: none; border-bottom: 2px solid transparent;
    color: var(--text-secondary); padding: 0.5rem 0.9rem; cursor: pointer;
    font-size: 0.9rem; border-radius: 6px 6px 0 0;
  }
  .tabs button:hover { background: var(--bg-tertiary); color: var(--text-primary); }
  .tabs button.active { color: var(--accent-light); border-bottom-color: var(--accent); }

  .chip {
    padding: 0.3rem 0.7rem; border-radius: 999px; font-size: 12px;
    cursor: pointer; background: var(--bg-tertiary); color: var(--text-secondary);
    border: 1px solid var(--border-color);
  }
  .chip:hover { background: var(--bg-hover); color: var(--text-primary); }
  .chip.attivo {
    background: var(--accent-bg); color: var(--accent-light);
    border-color: var(--accent);
  }

  .tabs button:focus-visible, .chip:focus-visible, .vista-btn:focus-visible,
  .btn-refresh:focus-visible, .btn-retry:focus-visible, .btn-altri:focus-visible {
    outline: 2px solid var(--accent); outline-offset: 2px;
  }

  .finestra { color: var(--text-muted); font-size: 12px; margin: 0 0 0.75rem; }
  .err { color: var(--danger); margin: 0.75rem 0; }
  .empty { color: var(--text-muted); margin-top: 1rem; }
  .errore-stato {
    display: flex; align-items: center; justify-content: space-between;
    gap: 1rem; flex-wrap: wrap; color: var(--danger); margin-top: 1rem;
  }

  .run-list, .job-list {
    list-style: none; display: flex; flex-direction: column; gap: 0.35rem;
  }
  .run-item, .job-item {
    animation: motion-rise var(--motion-enter) var(--motion-ease-out) backwards;
    animation-delay: calc(var(--i, 0) * var(--motion-stagger));
  }

  .btn-retry, .btn-altri {
    background: none; border: 1px solid var(--border-color);
    color: var(--accent-light); border-radius: 6px;
    padding: 0.45rem 0.8rem; cursor: pointer; font-size: 12px;
  }
  .btn-retry:hover, .btn-altri:hover:not(:disabled) { background: var(--bg-hover); }
  .btn-altri {
    display: block; margin: 0.8rem auto 0;
    background: var(--bg-tertiary); color: var(--text-primary);
  }
  .btn-altri:disabled { color: var(--text-muted); cursor: default; }

  @media (max-width: 768px) {
    .attivita { padding: 1rem; }
    .toolbar { align-items: flex-start; flex-direction: column; }
  }
</style>
