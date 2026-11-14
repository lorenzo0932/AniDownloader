<script>
  import { onMount } from 'svelte';
  import { pageEnter } from '../motion.js';
  import { api } from '../api.js';
  import Dropdown from '../Dropdown.svelte';
  import RunRow from './RunRow.svelte';
  import RunGroup from './RunGroup.svelte';
  import JobRow from './JobRow.svelte';

  const TABS = [['recenti', 'Recenti'], ['problemi', 'Problemi'], ['storico', 'Storico']];
  const ORIGINI = [['tutte', 'Tutte'], ['manuale', 'Manuale'], ['automatico', 'Automatico']];
  const ORIGINI_OPTIONS = ORIGINI.map(([value, label]) => ({ value, label }));

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
  let gruppiAperti = $state({});

  function chiaveGiorno(iso) {
    const d = new Date(iso);
    if (Number.isNaN(d.getTime())) return 'sconosciuto';
    return `${d.getFullYear()}-${String(d.getMonth() + 1).padStart(2, '0')}-${String(d.getDate()).padStart(2, '0')}`;
  }

  function etichettaGiorno(iso) {
    const d = new Date(iso);
    if (Number.isNaN(d.getTime())) return 'Senza data';
    const oggi = new Date();
    const inizioOggi = new Date(oggi.getFullYear(), oggi.getMonth(), oggi.getDate());
    const inizioGiorno = new Date(d.getFullYear(), d.getMonth(), d.getDate());
    const giorni = Math.round((inizioOggi - inizioGiorno) / 86400000);
    if (giorni === 0) return 'Oggi';
    if (giorni === 1) return 'Ieri';
    return d.toLocaleDateString('it-IT', { weekday: 'short', day: 'numeric', month: 'short' });
  }

  // Un check automatico senza episodi non merita una riga: è il rumore che
  // rendeva illeggibile la lista. I run manuali vuoti restano visibili,
  // perché sono un'azione esplicita dell'utente.
  function isCheckVuoto(run) {
    return run.origine === 'automatico'
      && (run.totale ?? 0) === 0
      && (run.non_riusciti ?? 0) === 0
      && run.stato === 'completato';
  }

  let gruppi = $derived.by(() => {
    if (vista !== 'avvii') return [];
    const perGiorno = new Map();
    const ordine = [];
    for (const run of items) {
      const key = chiaveGiorno(run.avviato_il);
      let gruppo = perGiorno.get(key);
      if (!gruppo) {
        gruppo = {
          key,
          label: etichettaGiorno(run.avviato_il),
          items: [],
          checks: [],
          episodi: 0,
          problemi: 0,
        };
        perGiorno.set(key, gruppo);
        ordine.push(gruppo);
      }
      if (isCheckVuoto(run)) {
        gruppo.checks.push(run);
      } else {
        gruppo.items.push(run);
        gruppo.episodi += run.completati ?? 0;
        gruppo.problemi += run.non_riusciti ?? 0;
      }
    }
    return ordine;
  });

  let episodiStats = $derived.by(() => {
    let completati = 0;
    let problemi = 0;
    let inCorso = 0;
    for (const job of items) {
      if (job.fase === 'completed') completati += 1;
      else if (job.fase === 'failed' || job.fase === 'interrupted') problemi += 1;
      else inCorso += 1;
    }
    return { completati, problemi, inCorso };
  });

  function toggleGruppo(key) {
    gruppiAperti = { ...gruppiAperti, [key]: !gruppiAperti[key] };
  }

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
    gruppiAperti = {};
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

  function deepLinkIniziale() {
    const predefinito = { vista: 'avvii', tab: 'recenti', run: '' };
    if (typeof window === 'undefined') return predefinito;

    const params = new URLSearchParams(window.location.search);
    const run = params.get('run') || '';
    // Il run ha priorità su tab/vista: la Home lo usa per aprire un avvio.
    if (run) return { vista: 'avvii', tab: 'recenti', run };

    const richiestoTab = params.get('tab');
    const tabValido = ['recenti', 'problemi', 'storico'].includes(richiestoTab)
      ? richiestoTab
      : 'recenti';
    // Vista Episodi opzionale; valori sconosciuti restano su Avvii.
    const vistaValida = params.get('vista') === 'episodi' ? 'episodi' : 'avvii';
    return { vista: vistaValida, tab: tabValido, run: '' };
  }

  onMount(() => {
    const avvia = async () => {
      const iniziale = deepLinkIniziale();
      vista = iniziale.vista;
      tab = iniziale.tab;
      await load();
      if (iniziale.run) await apriDeepLink(iniziale.run);
    };
    avvia();
    const timer = setInterval(poll, INTERVALLO_POLL);
    return () => clearInterval(timer);
  });
</script>

<div class="attivita" class:episodi={vista === 'episodi'} in:pageEnter|global>
  <div class="attivita-main">
  <header class="head">
    <div class="head-copy">
      <h1>Attività</h1>
      <p class="head-sub">{testoFinestra}</p>
    </div>
    <button class="btn-refresh" onclick={() => load()} disabled={loading}
            aria-label="Aggiorna attività">
      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"
           aria-hidden="true">
        <path d="M21 12a9 9 0 1 1-2.64-6.36"/>
        <polyline points="21 3 21 9 15 9"/>
      </svg>
      <span>{loading ? 'Aggiorno…' : 'Aggiorna'}</span>
    </button>
  </header>

  <div class="controls">
    <div class="viste" role="group" aria-label="Vista attività">
      <button class="vista-btn" aria-pressed={vista === 'avvii'}
              class:attivo={vista === 'avvii'}
              onclick={() => cambiaVista('avvii')}>Avvii</button>
      <button class="vista-btn" aria-pressed={vista === 'episodi'}
              class:attivo={vista === 'episodi'}
              onclick={() => cambiaVista('episodi')}>Episodi</button>
    </div>

    {#if vista === 'avvii'}
      <div class="origin-filter">
        <span class="filter-label">Origine</span>
        <Dropdown value={origine} options={ORIGINI_OPTIONS}
                  onchange={scegliOrigine} label="Filtra per origine"
                  id="attivita-origine" />
      </div>
    {/if}
  </div>

  <div class="tabs-row">
    <div class="tabs" role="tablist" aria-label="Attività">
      {#each TABS as [valore, label] (valore)}
        <button role="tab" id={`tab-${valore}`}
                aria-selected={tab === valore}
                aria-controls="panel-attivita"
                class:active={tab === valore}
                aria-label={`${label}${vista === 'avvii' && valore === 'problemi' && counts.problemi > 0
                  ? ` (${counts.problemi})` : ''}`}
                onclick={() => scegliTab(valore)}>
          {label}{#if vista === 'avvii' && valore === 'problemi' && counts.problemi > 0}
            <span class="tab-count" aria-hidden="true">{counts.problemi}</span>
          {/if}
        </button>
      {/each}
    </div>

    {#if vista === 'avvii'}
      <p class="period-summary">
        <strong>{counts.recenti ?? 0}</strong> avvii
        {#if (counts.problemi ?? 0) > 0}
          <span class="summary-sep" aria-hidden="true">·</span>
          <span class="ko"><strong>{counts.problemi}</strong> con problemi</span>
        {/if}
      </p>
    {:else}
      <p class="period-summary">
        <strong>{items.length}</strong> {items.length === 1 ? 'episodio' : 'episodi'}
        {#if episodiStats.problemi > 0}
          <span class="summary-sep" aria-hidden="true">·</span>
          <span class="ko"><strong>{episodiStats.problemi}</strong> con problemi</span>
        {/if}
      </p>
    {/if}
  </div>

  <div id="panel-attivita" role="tabpanel" aria-labelledby={`tab-${tab}`}>

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
        {#each gruppi as gruppo (gruppo.key)}
          <section class="day-group" aria-label={gruppo.label}>
            <header class="day-head">
              <span class="day-dot" aria-hidden="true"></span>
              <h3>{gruppo.label}</h3>
              <span class="day-line" aria-hidden="true"></span>
              <span class="day-meta">
                {#if gruppo.items.length > 0}
                  {gruppo.items.length} {gruppo.items.length === 1 ? 'avvio' : 'avvii'}
                {/if}
                {#if gruppo.episodi > 0}
                  <span class="sep" aria-hidden="true">·</span>
                  {gruppo.episodi} {gruppo.episodi === 1 ? 'episodio' : 'episodi'}
                {/if}
                {#if gruppo.problemi > 0}
                  <span class="sep" aria-hidden="true">·</span>
                  <span class="ko">{gruppo.problemi} non riusciti</span>
                {/if}
                {#if gruppo.checks.length > 0}
                  {#if gruppo.items.length > 0 || gruppo.episodi > 0}
                    <span class="sep" aria-hidden="true">·</span>
                  {/if}
                  {gruppo.checks.length} {gruppo.checks.length === 1 ? 'controllo' : 'controlli'}
                {/if}
              </span>
            </header>

            {#if gruppo.items.length > 0}
              <ul class="run-list">
                {#each gruppo.items as run, i (run.id)}
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
            {/if}

            {#if gruppo.checks.length > 0}
              <RunGroup
                runs={gruppo.checks}
                expanded={!!gruppiAperti[gruppo.key]}
                onToggle={() => toggleGruppo(gruppo.key)}
              />
            {/if}
          </section>
        {/each}
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

  <aside class="attivita-aside" aria-label="Riepilogo attività">
    <div class="aside-card">
      <h2>Riepilogo</h2>
      <div class="aside-stats">
        <div class="aside-stat">
          <span class="aside-dot in-corso" aria-hidden="true"></span>
          <span class="aside-label">In corso</span>
          <strong>{vista === 'avvii' ? (counts.in_corso ?? 0) : episodiStats.inCorso}</strong>
        </div>
        <div class="aside-stat">
          <span class="aside-dot ko" aria-hidden="true"></span>
          <span class="aside-label">Con problemi</span>
          <strong class:ko={(vista === 'avvii' ? (counts.problemi ?? 0) : episodiStats.problemi) > 0}>
            {vista === 'avvii' ? (counts.problemi ?? 0) : episodiStats.problemi}
          </strong>
        </div>
        <div class="aside-stat">
          <span class="aside-dot" aria-hidden="true"></span>
          <span class="aside-label">{vista === 'avvii' ? 'Recenti' : 'Completati'}</span>
          <strong>{vista === 'avvii' ? (counts.recenti ?? 0) : episodiStats.completati}</strong>
        </div>
      </div>
      {#if (vista === 'avvii' ? (counts.problemi ?? 0) : episodiStats.problemi) > 0}
        <button type="button" class="aside-link" onclick={() => scegliTab('problemi')}>
          Vedi i problemi
        </button>
      {/if}
      <p class="aside-hint">
        {#if vista === 'avvii'}
          I controlli senza novità sono raggruppati per giorno: non serve aprirli uno per uno.
        {:else}
          Gli episodi completati restano consultabili; i problemi sono evidenziati.
        {/if}
      </p>
    </div>
  </aside>
</div>

<style>
  .attivita {
    padding: var(--content-gutter, 1.5rem);
    max-width: var(--content-wide, 1680px);
    margin: 0 auto;
    display: grid;
    grid-template-columns: minmax(0, 1fr);
    gap: 1.5rem;
    align-items: start;
  }
  .attivita-main { min-width: 0; }
  .attivita-aside { display: none; min-width: 0; }
  .head { display: flex; align-items: flex-start; justify-content: space-between; gap: 1rem; }
  .head-copy { min-width: 0; display: flex; flex-direction: column; gap: 0.2rem; }
  .head h1 { font-size: 1.4rem; line-height: 1.1; }
  .head-sub { color: var(--text-muted); font-size: 12px; }
  .btn-refresh {
    display: inline-flex; align-items: center; gap: 0.4rem;
    background: none; border: 1px solid transparent; border-radius: 8px;
    color: var(--text-secondary); padding: 0.35rem 0.55rem;
    cursor: pointer; font-size: 12px; font-family: inherit; flex-shrink: 0;
  }
  .btn-refresh svg { width: 15px; height: 15px; }
  .btn-refresh:hover:not(:disabled) { background: var(--bg-tertiary); color: var(--text-primary); }
  .btn-refresh:disabled { color: var(--text-muted); cursor: default; }

  .controls {
    display: flex; flex-wrap: wrap; align-items: center; justify-content: space-between;
    gap: 0.75rem; margin: 1rem 0 0.25rem;
  }
  .viste {
    display: inline-flex; gap: 0.15rem; padding: 0.15rem;
    background: var(--bg-secondary); border: 1px solid var(--border-color);
    border-radius: 9px;
  }
  .vista-btn {
    background: none; border: none; color: var(--text-muted); border-radius: 7px;
    padding: 0.35rem 0.8rem; cursor: pointer; font-size: 0.82rem; font-family: inherit;
  }
  .vista-btn:hover { color: var(--text-primary); }
  .vista-btn.attivo { background: var(--accent-bg); color: var(--accent-light); }

  .origin-filter { display: flex; align-items: center; gap: 0.5rem; }
  .filter-label {
    font-size: 10px; color: var(--text-muted); text-transform: uppercase;
    letter-spacing: 0.05em;
  }
  .origin-filter :global(.dropdown-trigger) { min-height: 32px; font-size: 12px; }

  .tabs-row {
    display: flex; align-items: flex-end; justify-content: space-between; gap: 1rem;
    border-bottom: 1px solid var(--border-color); margin-bottom: 0.9rem;
  }
  .tabs { display: flex; gap: 0.25rem; }
  .tabs button {
    display: inline-flex; align-items: center; gap: 0.35rem;
    background: none; border: none; border-bottom: 2px solid transparent;
    color: var(--text-muted); padding: 0.55rem 0.8rem; cursor: pointer;
    font-size: 0.88rem; border-radius: 6px 6px 0 0; margin-bottom: -1px;
    font-family: inherit;
  }
  .tabs button:hover { background: var(--bg-tertiary); color: var(--text-primary); }
  .tabs button.active { color: var(--accent-light); border-bottom-color: var(--accent); }
  .tab-count {
    min-width: 17px; height: 17px; padding: 0 4px; border-radius: 999px;
    background: var(--danger-bg); color: var(--danger); font-size: 10px;
    font-weight: 700; display: inline-flex; align-items: center; justify-content: center;
  }
  .period-summary {
    color: var(--text-muted); font-size: 11px; padding-bottom: 0.65rem;
    white-space: nowrap;
  }
  .period-summary strong { color: var(--text-secondary); font-weight: 600; }
  .period-summary .ko { color: var(--danger); }
  .summary-sep { margin: 0 0.3rem; color: var(--text-muted-more); }

  .day-group { margin-bottom: 1.1rem; }
  .day-group:last-child { margin-bottom: 0; }
  .day-head { display: flex; align-items: center; gap: 0.5rem; margin: 0 0 0.45rem; }
  .day-dot {
    width: 6px; height: 6px; border-radius: 50%;
    background: var(--accent); flex: 0 0 auto;
  }
  .day-head h3 {
    font-size: 11px; font-weight: 600; letter-spacing: 0.05em;
    text-transform: uppercase; color: var(--text-secondary); white-space: nowrap;
  }
  .day-line { flex: 1; height: 1px; background: var(--border-color); min-width: 1rem; }
  .day-meta { color: var(--text-muted); font-size: 11px; white-space: nowrap; }
  .day-meta .sep { color: var(--text-muted-more); }
  .day-meta .ko { color: var(--danger); }

  .err { color: var(--danger); margin: 0.75rem 0; }
  .empty { color: var(--text-muted); margin-top: 1rem; }
  .errore-stato {
    display: flex; align-items: center; justify-content: space-between;
    gap: 1rem; flex-wrap: wrap; color: var(--danger); margin-top: 1rem;
  }

  .run-list, .job-list {
    list-style: none; display: flex; flex-direction: column; gap: 0.45rem;
  }
  .run-item, .job-item {
    animation: motion-rise var(--motion-enter) var(--motion-ease-out) backwards;
    animation-delay: calc(var(--i, 0) * var(--motion-stagger));
  }

  .btn-retry {
    background: none; border: 1px solid var(--border-color);
    color: var(--accent-light); border-radius: 6px;
    padding: 0.45rem 0.8rem; cursor: pointer; font-size: 12px;
  }
  .btn-retry:hover { background: var(--bg-hover); }
  .btn-altri {
    display: flex; width: fit-content; align-items: center; justify-content: center;
    margin: 1rem auto 0; padding: 0.5rem 0.9rem;
    background: none; border: none; border-radius: 7px;
    color: var(--accent-light); cursor: pointer; font-size: 12px;
    font-family: inherit;
  }
  .btn-altri:hover:not(:disabled) { background: var(--bg-tertiary); }
  .btn-altri:disabled { color: var(--text-muted); cursor: default; }

  .tabs button:focus-visible, .vista-btn:focus-visible,
  .btn-refresh:focus-visible, .btn-retry:focus-visible, .btn-altri:focus-visible,
  .aside-link:focus-visible {
    outline: 2px solid var(--accent); outline-offset: 2px;
  }

  .aside-card {
    background: var(--bg-secondary); border: 1px solid var(--border-color);
    border-radius: 12px; padding: 0.9rem;
  }
  .aside-card h2 {
    font-size: 10px; font-weight: 700; text-transform: uppercase;
    letter-spacing: 0.05em; color: var(--text-muted); margin-bottom: 0.75rem;
  }
  .aside-stats { display: flex; flex-direction: column; gap: 0.55rem; }
  .aside-stat { display: flex; align-items: center; gap: 0.5rem; }
  .aside-dot {
    width: 6px; height: 6px; border-radius: 50%; flex: 0 0 auto;
    background: var(--text-muted-more);
  }
  .aside-dot.in-corso { background: var(--accent); }
  .aside-dot.ko { background: var(--danger); }
  .aside-label { color: var(--text-muted); font-size: 12px; }
  .aside-stat strong {
    margin-left: auto; font-size: 13px; color: var(--text-primary);
    font-variant-numeric: tabular-nums;
  }
  .aside-stat strong.ko { color: var(--danger); }
  .aside-link {
    margin-top: 0.9rem; background: none; border: 1px solid var(--border-color);
    border-radius: 7px; color: var(--accent-light); font-size: 12px;
    padding: 0.4rem 0.6rem; cursor: pointer; font-family: inherit;
  }
  .aside-link:hover { background: var(--bg-hover); }
  .aside-hint { margin-top: 0.8rem; color: var(--text-muted); font-size: 11px; line-height: 1.5; }

  @media (min-width: 1280px) {
    .attivita { grid-template-columns: minmax(0, 1fr) 260px; gap: 1.25rem; }
    .attivita-aside { display: block; position: sticky; top: 1.5rem; }
    .period-summary { display: none; }
  }

  @media (max-width: 768px) {
    .attivita { padding: 1rem; }
    .controls { align-items: flex-start; flex-direction: column; }
    .tabs-row { flex-direction: column; align-items: stretch; gap: 0.25rem; }
    .period-summary { padding-bottom: 0.5rem; }
    .day-meta { display: none; }
  }
</style>
