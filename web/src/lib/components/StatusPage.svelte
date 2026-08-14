<script>
  import { onMount, onDestroy } from 'svelte';
  import { fly } from 'svelte/transition';
  import { api, posterUrl } from '../api.js';
  import ConfirmModal from './ConfirmModal.svelte';
  import OverallHeader from './OverallHeader.svelte';
  import ProgressList from './ProgressList.svelte';

  let seriesList = $state([]);
  let downloadRunning = $state(false);
  let overallStatus = $state('Pronto.');
  let sseConnected = $state(false);
  let sse = $state(null);
  let error = $state('');
  let logExpanded = $state(false);
  let skipExpanded = $state(false);
  let logEvents = $state([]);
  let route = $state('home');

  let seriesProgress = $state({});
  let hasRun = $state(false);
  let waitingExpanded = $state(false);

  let summary = $state(null);
  let stats = $state({ total: 0, done: 0, skipped: 0, errors: 0, dlTime: 0, convTime: 0 });
  let phase = $state('idle');
  let results = $state([]);
  let recentAdded = $state([]);
  let recentlyDownloaded = $state([]);

  async function loadRecentAdded() {
    try {
      const data = await api.series.list({ sort: 'added', dir: 'desc' });
      recentAdded = (data.series || []).slice(0, 10);
    } catch {}
  }

  async function loadRecentlyDownloaded() {
    try {
      const data = await api.series.list({ sort: 'last_downloaded_at', dir: 'desc' });
      recentlyDownloaded = (data.series || []).filter(s => s.last_downloaded_at).slice(0, 10);
    } catch {}
  }

  let taskValues = $derived(Object.values(seriesProgress));
  let activeCount = $derived(taskValues.filter(t => t.isActive).length);
  let doneCount = $derived(taskValues.filter(t => t.result === 'done').length);
  let skippedCount = $derived(taskValues.filter(t => t.result === 'skipped').length);
  let globalPercent = $derived.by(() => {
    let vals = taskValues.filter(t => t.isActive && t.percent > 0);
    if (vals.length === 0) return 0;
    return Math.round(vals.reduce((a, t) => a + t.percent, 0) / vals.length);
  });
  let activeTasks = $derived(taskValues.filter(t => t.episode > 0 && t.isActive));
  let doneTasks = $derived(taskValues.filter(t => t.result === 'done'));
  let skippedTasks = $derived(taskValues.filter(t => t.result === 'skipped'));
  let analysisSeries = $derived.by(() => {
    let tasksBySeries = new Map();
    for (let t of taskValues) {
      let existing = tasksBySeries.get(t.seriesName);
      if (!existing) { tasksBySeries.set(t.seriesName, []); existing = []; }
      existing.push(t);
    }
    return seriesList.filter(s => {
      let tasks = tasksBySeries.get(s.name);
      if (!tasks || tasks.length === 0) return false;
      return tasks.some(t => t.episode === 0 && t.result === null)
          && !tasks.some(t => t.episode > 0);
    });
  });
  let waitingSeries = $derived(seriesList.filter(s => !taskValues.some(t => t.seriesName === s.name)));

  function navigateTo(r) { route = r; }

  function updateProgress(name, msg, ep) {
    let taskKey = (ep > 0) ? `${name}_${ep}` : name;
    let displayMsg = (ep > 0) ? `(Ep ${ep}) ${msg}` : msg;
    let state = seriesProgress[taskKey];
    if (!state) {
      state = { seriesName: name, episode: ep || 0, key: taskKey, percent: 0, phaseMode: null, isActive: false, statusText: displayMsg, result: null };
      seriesProgress = { ...seriesProgress, [taskKey]: state };
      return;
    }
    if (state.result === 'done' || state.result === 'error') return;

    if (msg === 'MODE:BOTH' || msg === 'MODE:DL' || msg === 'MODE:CONV') {
      state.phaseMode = msg;
      state.isActive = true;
      state.statusText = displayMsg;
      seriesProgress = { ...seriesProgress };
      return;
    }

    if (msg.includes('\u2705')) {
      state.percent = 100;
      state.isActive = false;
      state.result = 'done';
      state.statusText = displayMsg;
      seriesProgress = { ...seriesProgress };
      return;
    }

    if (msg.includes('\u274C')) {
      state.isActive = false;
      state.result = 'error';
      state.statusText = displayMsg;
      seriesProgress = { ...seriesProgress };
      return;
    }

    if (msg.includes('\uD83D\uDFAB') || msg.toLowerCase().includes('saltato')) {
      state.isActive = false;
      state.result = 'skipped';
      state.statusText = displayMsg;
      seriesProgress = { ...seriesProgress };
      return;
    }

    let pct = msg.match(/(\d+)%/);
    if (pct) {
      let p = parseInt(pct[1]);
      if (state.phaseMode === 'BOTH') {
        state.percent = msg.includes('Conv') ? 50 + p / 2 : p / 2;
      } else {
        state.percent = p;
      }
      state.isActive = true;
      state.statusText = displayMsg;
      seriesProgress = { ...seriesProgress };
      return;
    }

    state.isActive = true;
    state.statusText = displayMsg;
    seriesProgress = { ...seriesProgress };
  }

  function addLog(ev) {
    logEvents = [...logEvents.slice(-199), ev];
  }

  async function loadStatus() {
    try {
      let [st, dl, seriesData] = await Promise.all([
        api.status(),
        api.download.status(),
        api.series.list()
      ]);
      downloadRunning = dl.running;
      seriesList = seriesData.series || [];
    } catch (e) {
      error = e.message;
    }
  }

  async function startDownload() {
    seriesProgress = {};
    logEvents = [];
    summary = null;
    stats = { total: 0, done: 0, skipped: 0, errors: 0, dlTime: 0, convTime: 0 };
    results = [];
    phase = 'analysis';
    hasRun = true;
    addLog({ type: 'overall', status: 'Avvio download...' });
    try {
      await api.download.start(false);
      downloadRunning = true;
    } catch (e) {
      addLog({ type: 'error', message: e.message });
    }
  }

  let stopWarning = $state(true);
  let showStopConfirm = $state(false);

  async function loadConfig() {
    try {
      const data = await api.config.get();
      stopWarning = data.config?.show_stop_warning !== false;
    } catch {
      addLog({ type: 'error', message: 'Impossibile caricare le impostazioni. Usa valori predefiniti.' });
    }
  }

  function stopClicked() {
    if (stopWarning) {
      showStopConfirm = true;
    } else {
      doStop();
    }
  }

  function doStop() {
    showStopConfirm = false;
    api.download.stop().catch(e => addLog({ type: 'error', message: e.message }));
  }

  function connectSse() {
    sse = api.sse();
    sseConnected = true;
    sse.onmessage = (event) => {
      try {
        let data = JSON.parse(event.data);
        addLog(data);

        switch (data.type) {
          case 'progress':
            updateProgress(data.series, data.message, data.episode);
            break;
          case 'overall':
            overallStatus = data.status;
            break;
          case 'phase':
            phase = data.phase;
            if (data.phase === 'processing') {
              overallStatus = 'Elaborazione in corso...';
            }
            break;
          case 'finished':
            stats.total++;
            if (data.success) {
              stats.done++;
              stats.dlTime += Number(data.dlTime || 0);
              stats.convTime += Number(data.convTime || 0);
            } else {
              stats.errors++;
            }
            let taskKey = (data.episode > 0) ? `${data.series}_${data.episode}` : data.series;
            let suffix = data.episode ? ` (Ep ${data.episode})` : '';
            results = [...results, { name: data.series + suffix, success: data.success, error: data.error || null, dlTime: Number(data.dlTime || 0), convTime: Number(data.convTime || 0), skipped: false }];
            if (seriesProgress[taskKey]) {
              let st = seriesProgress[taskKey];
              st.percent = 100;
              st.isActive = false;
              st.result = data.success ? 'done' : 'error';
              st.statusText = data.success ? (suffix + ' ✅ Completato') : 'Errore: ' + (data.error || '');
              seriesProgress = { ...seriesProgress };
            }
            break;
          case 'skipped':
            stats.total++;
            stats.skipped++;
            results = [...results, { name: data.series, success: true, skipped: true, error: null, dlTime: 0, convTime: 0 }];
            if (seriesProgress[data.series]) {
              let st = seriesProgress[data.series];
              st.isActive = false;
              st.result = 'skipped';
              st.statusText = data.reason;
              seriesProgress = { ...seriesProgress };
            } else {
              seriesProgress = { ...seriesProgress, [data.series]: { seriesName: data.series, episode: 0, key: data.series, percent: 0, phaseMode: null, isActive: false, statusText: data.reason, result: 'skipped' } };
            }
            break;
          case 'done':
            downloadRunning = false;
            overallStatus = 'Completato.';
            phase = 'done';
            summary = { ...stats };
            loadRecentAdded();
            loadRecentlyDownloaded();
            break;
        }
      } catch { /* ignore parse errors */ }
    };
    sse.onerror = () => { sseConnected = false; };
  }

  onMount(() => {
    loadConfig();
    loadStatus();
    connectSse();
    loadRecentAdded();
    loadRecentlyDownloaded();
  });

  onDestroy(() => {
    if (sse) sse.close();
  });
</script>

<div class="vetrina" in:fly={{ y: 8, duration: 200 }}>
  <OverallHeader
    {downloadRunning}
    {overallStatus}
    {sseConnected}
    {phase}
    {summary}
    {globalPercent}
    {activeCount}
    {doneCount}
    {skippedCount}
    onstart={startDownload}
    onstop={stopClicked}
  />

  {#if error}
    <div class="error">{error}</div>
  {/if}

  {#if summary}
    <div class="summary-box">
      <svg class="summary-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M22 11.08V12a10 10 0 11-5.93-9.14"/><polyline points="22 4 12 14.01 9 11.01"/></svg>
      <div class="summary-body">
        <span class="summary-title">Download completato</span>
        <div class="summary-stats">
          <span class="stat stat-done">{summary.done} completate</span>
          <span class="stat stat-skipped">{summary.skipped} saltate</span>
          <span class="stat stat-error">{summary.errors} errori</span>
          <span class="stat stat-total">{summary.total} totali</span>
        </div>
        {#if results.length > 0}
          <div class="summary-lists">
            {#each results.filter(r => r.success && !r.skipped) as r}
              <div class="summary-list done">✅ {r.name}</div>
            {/each}
            {#each results.filter(r => !r.success && !r.skipped) as r}
              <div class="summary-list error">❌ {r.name} — {r.error}</div>
            {/each}
          </div>
        {/if}
        {#if stats.dlTime > 0 || stats.convTime > 0}
          <div class="summary-times">
            Download: {stats.dlTime.toFixed(1)}s &middot; Conversione: {stats.convTime.toFixed(1)}s
          </div>
        {/if}
      </div>
      <div class="summary-actions">
        <button class="btn-primary btn-sm" onclick={startDownload}>Nuovo Download</button>
        <button class="btn-secondary btn-sm" onclick={() => { history.pushState(null, '', '/gestione'); window.dispatchEvent(new PopStateEvent('popstate')); }}>
          <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" width="14" height="14"><line x1="8" y1="6" x2="21" y2="6"/><line x1="8" y1="12" x2="21" y2="12"/><line x1="8" y1="18" x2="21" y2="18"/><line x1="3" y1="6" x2="3.01" y2="6"/><line x1="3" y1="12" x2="3.01" y2="12"/><line x1="3" y1="18" x2="3.01" y2="18"/></svg>
          Gestione Serie
        </button>
      </div>
    </div>
  {/if}

  {#if !hasRun && taskValues.length === 0 && !summary}
    <div class="dashboard-tables">
      <div class="table-col">
        <div class="group-label">Ultime serie aggiunte</div>
        <div class="series-list">
          {#if recentAdded.length > 0}
            {#each recentAdded as s (s.name)}
              <div class="series-row">
                <img class="poster-thumb" src={posterUrl(s.path)} alt="" loading="lazy" />
                <div class="series-info">
                  <span class="series-name">{s.name}</span>
                  <span class="service-badge">{s.service === 'animeU_scraper' ? 'AnimeU' : 'AnimeW'}</span>
                </div>
              </div>
            {/each}
          {:else}
            <p class="empty-text">Nessuna serie aggiunta</p>
          {/if}
        </div>
      </div>
      <div class="table-col">
        <div class="group-label">Ultime serie scaricate</div>
        <div class="series-list">
          {#if recentlyDownloaded.length > 0}
            {#each recentlyDownloaded as s (s.name)}
              <div class="series-row">
                <img class="poster-thumb" src={posterUrl(s.path)} alt="" loading="lazy" />
                <div class="series-info">
                  <span class="series-name">{s.name}</span>
                  <span class="service-badge">{s.service === 'animeU_scraper' ? 'AnimeU' : 'AnimeW'}</span>
                </div>
              </div>
            {/each}
          {:else}
            <p class="empty-text">Ancora nessun episodio scaricato</p>
          {/if}
        </div>
      </div>
    </div>
  {:else if summary}
    <div class="dashboard-tables">
      <div class="table-col">
        <div class="group-label">Ultime serie aggiunte</div>
        <div class="series-list">
          {#if recentAdded.length > 0}
            {#each recentAdded as s (s.name)}
              <div class="series-row">
                <img class="poster-thumb" src={posterUrl(s.path)} alt="" loading="lazy" />
                <div class="series-info">
                  <span class="series-name">{s.name}</span>
                  <span class="service-badge">{s.service === 'animeU_scraper' ? 'AnimeU' : 'AnimeW'}</span>
                </div>
              </div>
            {/each}
          {:else}
            <p class="empty-text">Nessuna serie aggiunta</p>
          {/if}
        </div>
      </div>
      <div class="table-col">
        <div class="group-label">Ultime serie scaricate</div>
        <div class="series-list">
          {#if recentlyDownloaded.length > 0}
            {#each recentlyDownloaded as s (s.name)}
              <div class="series-row">
                <img class="poster-thumb" src={posterUrl(s.path)} alt="" loading="lazy" />
                <div class="series-info">
                  <span class="series-name">{s.name}</span>
                  <span class="service-badge">{s.service === 'animeU_scraper' ? 'AnimeU' : 'AnimeW'}</span>
                </div>
              </div>
            {/each}
          {:else}
            <p class="empty-text">Ancora nessun episodio scaricato</p>
          {/if}
        </div>
      </div>
    </div>
  {:else if !summary}
    <ProgressList
      {analysisSeries}
      {activeTasks}
      {doneTasks}
      {skippedTasks}
      {waitingSeries}
    />
  {/if}

  <ConfirmModal
    show={showStopConfirm}
    title="Interrompere il download?"
    message={'Sei sicuro di voler interrompere il download in corso?\n\nI file parzialmente scaricati verranno rimossi se l\'opzione "Pulizia Automatica" è attiva nelle impostazioni.'}
    confirmText="Ferma Download"
    danger={true}
    onConfirm={doStop}
    onCancel={() => showStopConfirm = false}
  />

  {#if logEvents.length > 0}
    <div class="log-section">
      <button class="log-toggle" onclick={() => logExpanded = !logExpanded}>
        Log eventi ({logEvents.length}) {logExpanded ? '\u25BC' : '\u25B6'}
      </button>
      {#if logExpanded}
        <div class="log-view">
          {#each logEvents as ev, idx (idx)}
            <div class="log-line type-{ev.type}">
              {#if ev.type === 'progress'}
                <strong>{ev.series}</strong>{ev.episode ? ` (Ep ${ev.episode})` : ''}: {ev.message}
              {:else if ev.type === 'overall'}
                <em>{ev.status}</em>
              {:else if ev.type === 'finished'}
                <strong>{ev.series}</strong>{ev.episode ? ` (Ep ${ev.episode})` : ''}: {ev.success ? 'Completato' : 'Errore'} ({Number(ev.dlTime).toFixed(1)}s / {Number(ev.convTime).toFixed(1)}s)
              {:else if ev.type === 'skipped'}
                <strong>{ev.series}</strong>: saltato -- {ev.reason}
              {:else if ev.type === 'done'}
                <span class="done-msg">Elaborazione completata.</span>
              {:else}
                {JSON.stringify(ev)}
              {/if}
            </div>
          {/each}
        </div>
      {/if}
    </div>
  {/if}
</div>

<style>
  .vetrina { display: flex; flex-direction: column; gap: 1rem; }

  .btn-primary {
    padding: 0.6rem 1.25rem; background: var(--accent); border: none; border-radius: 8px;
    color: #fff; font-size: 0.85rem; font-weight: 600; cursor: pointer;
  }
  .btn-primary:hover { background: var(--accent-hover); }
  .btn-secondary {
    padding: 0.6rem 1.25rem; background: var(--bg-tertiary); border: 1px solid var(--border-color); border-radius: 8px;
    color: var(--text-secondary); font-size: 0.85rem; font-weight: 600; cursor: pointer;
    display: flex; align-items: center; gap: 0.4rem;
  }
  .btn-secondary:hover { background: var(--bg-hover); color: var(--text-primary); }
  .btn-sm { padding: 0.45rem 0.9rem; font-size: 0.8rem; }

  .error {
    background: var(--danger-bg); border: 1px solid var(--danger-border); color: var(--danger);
    padding: 0.75rem; border-radius: 8px; font-size: 0.85rem;
  }

  .summary-box {
    display: flex; align-items: flex-start; gap: 1rem;
    background: var(--bg-secondary); border: 1px solid var(--border-color); border-radius: 12px;
    padding: 1.25rem;
  }
  .summary-icon { width: 28px; height: 28px; color: var(--success); flex-shrink: 0; margin-top: 2px; }
  .summary-body { flex: 1; }
  .summary-title { font-size: 1rem; font-weight: 700; color: var(--text-primary); display: block; margin-bottom: 0.5rem; }
  .summary-stats { display: flex; flex-wrap: wrap; gap: 0.75rem; margin-bottom: 0.35rem; }
  .stat { font-size: 0.8rem; font-weight: 600; }
  .stat-done { color: var(--success); }
  .stat-skipped { color: var(--warning); }
  .stat-error { color: var(--danger); }
  .stat-total { color: var(--text-muted); }
  .summary-times { font-size: 0.78rem; color: var(--text-muted-more); }
  .summary-lists { margin-top: 0.5rem; display: flex; flex-direction: column; gap: 0.2rem; }
  .summary-list { font-size: 0.8rem; line-height: 1.4; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
  .summary-list.done { color: var(--success); }
  .summary-list.error { color: var(--danger); }
  .summary-actions { display: flex; gap: 0.5rem; flex-shrink: 0; align-items: flex-start; }

  .dashboard-tables {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(320px, 1fr));
    gap: 1.5rem;
  }
  .table-col {
    display: flex;
    flex-direction: column;
    gap: 0.25rem;
  }
  .service-badge {
    font-size: 0.68rem; color: var(--accent);
  }
  .empty-text {
    text-align: center; padding: 1rem; color: var(--text-muted); font-size: 0.85rem;
  }

  .series-list { display: flex; flex-direction: column; gap: 0.25rem; }
  .group-label {
    font-size: 0.8rem; font-weight: 600; color: var(--accent-light);
    text-transform: uppercase; letter-spacing: 0.05em;
    padding: 0.75rem 0 0.25rem; border-bottom: 1px solid var(--border-color); margin-bottom: 0.25rem;
  }
  .series-row {
    display: flex; align-items: center; gap: 0.75rem;
    background: var(--bg-secondary); border-radius: 8px; padding: 0.5rem 0.75rem;
    border: 1px solid var(--border-color);
    animation: rowIn 0.3s ease-out both;
  }
  .poster-thumb {
    width: 40px; height: 56px; border-radius: 4px; object-fit: cover;
    background: var(--bg-tertiary); flex-shrink: 0;
  }
  .series-info { flex: 1; min-width: 0; display: flex; flex-direction: column; gap: 0.15rem; }
  .series-name { font-size: 0.85rem; font-weight: 500; color: var(--text-primary); white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }

  .log-section { margin-top: 0.5rem; }
  .log-toggle {
    background: none; border: none; color: var(--text-muted); cursor: pointer;
    font-size: 0.8rem; padding: 0.25rem 0;
  }
  .log-toggle:hover { color: var(--text-secondary); }
  .log-view {
    background: var(--log-bg); border: 1px solid var(--border-color); border-radius: 8px;
    padding: 0.75rem; font-family: 'Consolas', 'Menlo', 'Monaco', 'DejaVu Sans Mono', 'Noto Sans Mono', 'Courier New', monospace;
    font-weight: 500; font-size: 0.78rem; line-height: 1.5; color: var(--text-secondary);
    max-height: 200px; overflow-y: auto; margin-top: 0.25rem;
  }
  .log-line { white-space: nowrap; }
  .log-line :global(strong) { color: var(--accent-light); }
  .log-line :global(em) { color: var(--text-muted); }
  .done-msg { color: var(--success); }
  :global(.type-finished) { color: var(--success); }

  @keyframes rowIn {
    from { opacity: 0; transform: translateX(-8px); }
    to { opacity: 1; transform: translateX(0); }
  }

  @media (max-width: 768px) {
    .summary-box { flex-direction: column; align-items: stretch; gap: 0.75rem; }
    .summary-actions { flex-direction: column; align-items: stretch; }
    .summary-actions .btn-primary,
    .summary-actions .btn-secondary { width: 100%; justify-content: center; }
    .summary-icon { display: none; }

    .series-row { padding: 0.4rem 0.5rem; gap: 0.5rem; flex-wrap: nowrap; }
    .poster-thumb { width: 32px; height: 44px; }
    .series-name { font-size: 0.8rem; }

    .log-view { font-size: 0.65rem; max-height: 150px; }
  }
</style>
