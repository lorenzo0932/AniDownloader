<script>
  import { onMount, onDestroy } from 'svelte';
  import { fly } from 'svelte/transition';
  import { api } from '../api.js';

  let seriesList = $state([]);
  let downloadRunning = $state(false);
  let overallStatus = $state('Pronto.');
  let sseConnected = $state(false);
  let sse = $state(null);
  let error = $state('');
  let logExpanded = $state(false);
  let logEvents = $state([]);
  let route = $state('home');

  let seriesProgress = $state({});
  let hasRun = $state(false);
  let waitingExpanded = $state(false);

  let summary = $state(null);
  let stats = $state({ total: 0, done: 0, skipped: 0, errors: 0, dlTime: 0, convTime: 0 });

  let activeCount = $derived(seriesList.filter(s => seriesProgress[s.name]?.isActive).length);
  let doneCount = $derived(seriesList.filter(s => seriesProgress[s.name]?.result === 'done').length);
  let skippedCount = $derived(seriesList.filter(s => seriesProgress[s.name]?.result === 'skipped').length);
  let globalPercent = $derived.by(() => {
    let vals = Object.values(seriesProgress).filter(s => s.isActive && s.percent > 0);
    if (vals.length === 0) return 0;
    return Math.round(vals.reduce((a, s) => a + s.percent, 0) / vals.length);
  });
  let activeSeries = $derived(seriesList.filter(s => seriesProgress[s.name]?.isActive));
  let doneSeries = $derived(seriesList.filter(s => seriesProgress[s.name]?.result === 'done'));
  let skippedSeries = $derived(seriesList.filter(s => seriesProgress[s.name]?.result === 'skipped'));
  let waitingSeries = $derived(seriesList.filter(s => !seriesProgress[s.name]));

  function navigateTo(r) { route = r; }

  function updateProgress(name, msg) {
    let state = seriesProgress[name];
    if (!state) {
      state = { percent: 0, phaseMode: null, isActive: true, statusText: msg, result: null };
      seriesProgress = { ...seriesProgress, [name]: state };
      return;
    }

    if (msg === 'MODE:BOTH' || msg === 'MODE:DL' || msg === 'MODE:CONV') {
      state.phaseMode = msg;
      state.isActive = true;
      state.statusText = msg;
      seriesProgress = { ...seriesProgress };
      return;
    }

    if (msg.includes('\u2705')) {
      state.percent = 100;
      state.isActive = false;
      state.result = 'done';
      state.statusText = msg;
      seriesProgress = { ...seriesProgress };
      return;
    }

    if (msg.includes('\u274C')) {
      state.isActive = false;
      state.result = 'error';
      state.statusText = msg;
      seriesProgress = { ...seriesProgress };
      return;
    }

    if (msg.includes('\uD83D\uDFAB') || msg.toLowerCase().includes('saltato')) {
      state.isActive = false;
      state.result = 'skipped';
      state.statusText = msg;
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
      state.statusText = msg;
      seriesProgress = { ...seriesProgress };
      return;
    }

    state.isActive = true;
    state.statusText = msg;
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
    hasRun = true;
    addLog({ type: 'overall', status: 'Avvio download...' });
    try {
      await api.download.start(false);
      downloadRunning = true;
    } catch (e) {
      addLog({ type: 'error', message: e.message });
    }
  }

  async function stopDownload() {
    try {
      await api.download.stop();
    } catch (e) {
      addLog({ type: 'error', message: e.message });
    }
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
            updateProgress(data.series, data.message);
            break;
          case 'overall':
            overallStatus = data.status;
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
            if (seriesProgress[data.series]) {
              let st = seriesProgress[data.series];
              st.percent = 100;
              st.isActive = false;
              st.result = data.success ? 'done' : 'error';
              if (!data.success) st.statusText = 'Errore: ' + (data.error || '');
              seriesProgress = { ...seriesProgress };
            }
            break;
          case 'skipped':
            stats.total++;
            stats.skipped++;
            if (seriesProgress[data.series]) {
              let st = seriesProgress[data.series];
              st.isActive = false;
              st.result = 'skipped';
              st.statusText = data.reason;
              seriesProgress = { ...seriesProgress };
            } else {
              seriesProgress = { ...seriesProgress, [data.series]: { percent: 0, phaseMode: null, isActive: false, statusText: data.reason, result: 'skipped' } };
            }
            break;
          case 'done':
            downloadRunning = false;
            overallStatus = 'Completato.';
            summary = { ...stats };
            break;
        }
      } catch { /* ignore parse errors */ }
    };
    sse.onerror = () => { sseConnected = false; };
  }

  onMount(() => {
    loadStatus();
    connectSse();
  });

  onDestroy(() => {
    if (sse) sse.close();
  });
</script>

<div class="vetrina" in:fly={{ y: 8, duration: 200 }}>
  <div class="toolbar">
    <button class="btn-primary" onclick={startDownload} disabled={downloadRunning}>
      Avvia Download
    </button>
    <button class="btn-danger" onclick={stopDownload} disabled={!downloadRunning}>
      Ferma Download
    </button>
    <span class="status-label">{overallStatus}</span>
    <span class="sse-badge" class:connected={sseConnected}>
      {sseConnected ? 'SSE' : 'Disconnesso'}
    </span>
  </div>

  {#if globalPercent > 0}
    <div class="global-bar-wrap">
      <div class="global-bar">
        <div class="global-bar-fill" style="width:{globalPercent}%"></div>
      </div>
      <span class="global-bar-text">{globalPercent}% ({activeCount} attive, {doneCount} completate, {skippedCount} saltate)</span>
    </div>
  {/if}

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
        {#if stats.dlTime > 0 || stats.convTime > 0}
          <div class="summary-times">
            Download: {stats.dlTime.toFixed(1)}s &middot; Conversione: {stats.convTime.toFixed(1)}s
          </div>
        {/if}
      </div>
      <div class="summary-actions">
        <button class="btn-primary btn-sm" onclick={startDownload}>Nuovo Download</button>
        <button class="btn-secondary btn-sm" onclick={() => window.location.hash = '#gestione'}>
          <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" width="14" height="14"><line x1="8" y1="6" x2="21" y2="6"/><line x1="8" y1="12" x2="21" y2="12"/><line x1="8" y1="18" x2="21" y2="18"/><line x1="3" y1="6" x2="3.01" y2="6"/><line x1="3" y1="12" x2="3.01" y2="12"/><line x1="3" y1="18" x2="3.01" y2="18"/></svg>
          Gestione Serie
        </button>
      </div>
    </div>
  {/if}

  {#if !hasRun && activeSeries.length === 0 && doneSeries.length === 0 && skippedSeries.length === 0 && !summary}
    <div class="welcome">
      <svg class="welcome-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5"><circle cx="12" cy="12" r="10"/><polyline points="8 12 12 16 16 12"/><line x1="12" y1="8" x2="12" y2="16"/></svg>
      <p class="welcome-title">Nessun download in corso</p>
      <p class="welcome-sub">Premi <strong>Avvia Download</strong> per controllare nuovi episodi.</p>
    </div>
  {:else if !summary}
    <div class="series-list">
      {#if activeSeries.length > 0}
        <div class="group-label">In elaborazione ({activeSeries.length})</div>
        {#each activeSeries as s (s.name)}
          {@const prog = seriesProgress[s.name]}
          <div class="series-row active">
            <img class="poster-thumb" src="/api/poster?path={encodeURIComponent(s.path)}" alt="" loading="lazy" />
            <div class="series-info">
              <span class="series-name">{s.name}</span>
              <span class="series-status">{prog.statusText}</span>
            </div>
            <div class="pbar-wrap">
              <div class="pbar">
                <div class="pbar-fill" style="width:{prog.percent}%"></div>
              </div>
              <span class="pbar-text">{Math.round(prog.percent)}%</span>
            </div>
          </div>
        {/each}
      {/if}

      {#if doneSeries.length > 0}
        <div class="group-label">Completate ({doneSeries.length})</div>
        {#each doneSeries as s (s.name)}
          {@const prog = seriesProgress[s.name]}
          <div class="series-row done">
            <img class="poster-thumb" src="/api/poster?path={encodeURIComponent(s.path)}" alt="" loading="lazy" />
            <div class="series-info">
              <span class="series-name">{s.name}</span>
              <span class="series-status ok">{prog.statusText}</span>
            </div>
          </div>
        {/each}
      {/if}

      {#if skippedSeries.length > 0}
        <div class="group-label">
          <button class="group-toggle" onclick={() => logExpanded = !logExpanded}>
            Saltate ({skippedSeries.length}) {logExpanded ? '\u25BC' : '\u25B6'}
          </button>
        </div>
        {#if logExpanded}
          {#each skippedSeries as s (s.name)}
            {@const prog = seriesProgress[s.name]}
            <div class="series-row skipped">
              <div class="series-info">
                <span class="series-name">{s.name}</span>
                <span class="series-status skip">{prog.statusText}</span>
              </div>
            </div>
          {/each}
        {/if}
      {/if}

      {#if waitingSeries.length > 0}
        <div class="group-label">
          <button class="group-toggle" onclick={() => waitingExpanded = !waitingExpanded}>
            In attesa ({waitingSeries.length}) {waitingExpanded ? '\u25BC' : '\u25B6'}
          </button>
        </div>
        {#if waitingExpanded}
          {#each waitingSeries as s (s.name)}
            <div class="series-row waiting">
              <div class="series-info">
                <span class="series-name">{s.name}</span>
              </div>
            </div>
          {/each}
        {/if}
      {/if}
    </div>
  {/if}

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
                <strong>{ev.series}</strong>: {ev.message}
              {:else if ev.type === 'overall'}
                <em>{ev.status}</em>
              {:else if ev.type === 'finished'}
                <strong>{ev.series}</strong>: {ev.success ? 'Completato' : 'Errore'} ({Number(ev.dlTime).toFixed(1)}s / {Number(ev.convTime).toFixed(1)}s)
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

  .toolbar { display: flex; align-items: center; gap: 0.75rem; flex-wrap: wrap; }
  .btn-primary {
    padding: 0.6rem 1.25rem; background: var(--accent); border: none; border-radius: 8px;
    color: #fff; font-size: 0.85rem; font-weight: 600; cursor: pointer;
  }
  .btn-primary:hover { background: var(--accent-hover); }
  .btn-primary:disabled { opacity: 0.5; cursor: not-allowed; }
  .btn-danger {
    padding: 0.6rem 1.25rem; background: var(--danger-bg); border: 1px solid var(--danger-border); border-radius: 8px;
    color: var(--danger); font-size: 0.85rem; font-weight: 600; cursor: pointer;
  }
  .btn-danger:hover { background: var(--danger-bg-hover); }
  .btn-danger:disabled { opacity: 0.5; cursor: not-allowed; }
  .btn-secondary {
    padding: 0.6rem 1.25rem; background: var(--bg-tertiary); border: 1px solid var(--border-color); border-radius: 8px;
    color: var(--text-secondary); font-size: 0.85rem; font-weight: 600; cursor: pointer;
    display: flex; align-items: center; gap: 0.4rem;
  }
  .btn-secondary:hover { background: var(--bg-hover); color: var(--text-primary); }
  .btn-sm { padding: 0.45rem 0.9rem; font-size: 0.8rem; }
  .status-label { font-size: 0.9rem; color: var(--text-secondary); font-weight: 600; flex: 1; }
  .sse-badge {
    font-size: 0.75rem; color: var(--text-muted); background: var(--bg-secondary); border: 1px solid var(--border-color);
    padding: 0.2rem 0.5rem; border-radius: 4px;
  }
  .sse-badge.connected { color: var(--success); border-color: var(--success-border); }

  .global-bar-wrap { display: flex; align-items: center; gap: 0.75rem; }
  .global-bar {
    flex: 1; height: 10px; background: var(--border-color); border-radius: 5px; overflow: hidden;
  }
  .global-bar-fill {
    height: 100%; background: linear-gradient(90deg, var(--accent), var(--accent-light));
    border-radius: 5px; transition: width 0.3s ease;
  }
  .global-bar-text { font-size: 0.8rem; color: var(--text-muted); white-space: nowrap; }

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
  .summary-actions { display: flex; gap: 0.5rem; flex-shrink: 0; align-items: flex-start; }

  .welcome {
    display: flex; flex-direction: column; align-items: center; justify-content: center;
    padding: 4rem 2rem; text-align: center;
  }
  .welcome-icon { width: 48px; height: 48px; color: var(--text-muted-more); margin-bottom: 1rem; }
  .welcome-title { font-size: 1.1rem; color: var(--text-muted); margin-bottom: 0.5rem; }
  .welcome-sub { font-size: 0.85rem; color: var(--text-muted); }
  .welcome-sub strong { color: var(--accent-light); }

  .series-list { display: flex; flex-direction: column; gap: 0.25rem; }
  .group-label {
    font-size: 0.8rem; font-weight: 600; color: var(--accent-light);
    text-transform: uppercase; letter-spacing: 0.05em;
    padding: 0.75rem 0 0.25rem; border-bottom: 1px solid var(--border-color); margin-bottom: 0.25rem;
  }
  .group-toggle {
    background: none; border: none; color: var(--accent-light); cursor: pointer;
    font-size: 0.8rem; font-weight: 600; text-transform: uppercase; letter-spacing: 0.05em;
    padding: 0;
  }
  .group-toggle:hover { color: var(--accent); }

  .series-row {
    display: flex; align-items: center; gap: 0.75rem;
    background: var(--bg-secondary); border-radius: 8px; padding: 0.5rem 0.75rem;
    border: 1px solid var(--border-color);
    animation: rowIn 0.3s ease-out both;
  }
  .series-row:nth-child(2) { animation-delay: 20ms; }
  .series-row:nth-child(3) { animation-delay: 40ms; }
  .series-row:nth-child(4) { animation-delay: 60ms; }
  .series-row:nth-child(5) { animation-delay: 80ms; }
  .series-row.active { border-color: var(--accent-bg); }
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

  .log-section { margin-top: 0.5rem; }
  .log-toggle {
    background: none; border: none; color: var(--text-muted); cursor: pointer;
    font-size: 0.8rem; padding: 0.25rem 0;
  }
  .log-toggle:hover { color: var(--text-secondary); }
  .log-view {
    background: var(--log-bg); border: 1px solid var(--border-color); border-radius: 8px;
    padding: 0.75rem; font-family: 'Fira Code', 'Cascadia Code', monospace;
    font-size: 0.72rem; line-height: 1.5; color: var(--text-muted);
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
    .toolbar { flex-direction: column; align-items: stretch; }
    .toolbar .btn-primary, .toolbar .btn-danger { width: 100%; justify-content: center; }
    .status-label { text-align: center; }
    .sse-badge { align-self: flex-end; }

    .summary-box { flex-direction: column; align-items: stretch; gap: 0.75rem; }
    .summary-actions { flex-direction: column; align-items: stretch; }
    .summary-actions .btn-primary,
    .summary-actions .btn-secondary { width: 100%; justify-content: center; }
    .summary-icon { display: none; }

    .global-bar-wrap { flex-direction: column; align-items: stretch; gap: 0.25rem; }
    .global-bar-text { white-space: normal; text-align: center; }

    .series-row { padding: 0.4rem 0.5rem; gap: 0.5rem; flex-wrap: nowrap; }
    .poster-thumb { width: 32px; height: 44px; }
    .pbar-wrap { min-width: 100px; }
    .pbar-text { width: 2rem; font-size: 0.7rem; }
    .series-name { font-size: 0.8rem; }
    .series-status { font-size: 0.7rem; }

    .log-view { font-size: 0.65rem; max-height: 150px; }

    .welcome { padding: 2rem 1rem; }
    .welcome-icon { width: 36px; height: 36px; }
    .welcome-title { font-size: 1rem; }
    .welcome-sub { font-size: 0.8rem; }
  }
</style>
