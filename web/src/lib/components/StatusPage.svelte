<script>
  import { onMount, onDestroy } from 'svelte';
  import { pageEnter } from '../motion.js';
  import { api, BASE, posterUrl, posterSrcSet } from '../api.js';
  import ConfirmModal from './ConfirmModal.svelte';
  import DetailModal from './DetailModal.svelte';
  import OverallHeader from './OverallHeader.svelte';
  import ProgressList from './ProgressList.svelte';
  import RecentSeriesRow from './RecentSeriesRow.svelte';
  import DownloadEventLog from './DownloadEventLog.svelte';
  import { emptyDownloadState, applyDownloadEvent } from '../downloadState.js';

  let seriesList = $state([]);
  let run = $state(emptyDownloadState());
  let starting = $state(false);
  let hydration = $state(0);
  let downloadRunning = $derived(run.running);
  let overallStatus = $derived(run.status);
  let sseConnected = $state(false);
  let sse = $state.raw(null);
  let error = $state('');
  let hasRun = $derived(run.runId > 0);
  let phase = $derived(run.phase);
  let stats = $derived({ total: run.tasks.length,
    done: run.tasks.filter(t => t.phase === 'completed').length,
    skipped: run.tasks.filter(t => t.phase === 'skipped').length,
    errors: run.tasks.filter(t => t.phase === 'error').length,
    interrupted: run.tasks.filter(t => t.phase === 'interrupted').length,
    dlTime: run.tasks.reduce((n, t) => n + (t.dlTime || 0), 0),
    convTime: run.tasks.reduce((n, t) => n + (t.convTime || 0), 0),
  });
  let summary = $derived(phase === 'done' ? stats : null);
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

  let taskValues = $derived(run.tasks);
  let waitingSeries = $derived(run.running ? seriesList.filter(s =>
    !taskValues.some(t => t.seriesName === s.name)) : []);
  let syncing = false;
  let buffered = [];
  let disposed = false;
  let resyncRequested = false;

  function refreshStatus() {
    if (syncing) resyncRequested = true;
    else loadStatus();
  }

  async function loadStatus() {
    if (syncing || disposed) return;
    syncing = true;
    buffered = [];
    try {
      let [dl, seriesData] = await Promise.all([
        api.download.status(),
        api.series.list()
      ]);
      if (disposed) return;
      run = { ...emptyDownloadState(), ...dl };
      hydration++;
      seriesList = seriesData.series || [];
      error = '';
    } catch (e) {
      error = e.message;
    } finally {
      if (!disposed) for (const event of buffered) {
        if (event.sequence > run.sequence + 1) resyncRequested = true;
        run = applyDownloadEvent(run, event);
      }
      buffered = [];
      syncing = false;
      if (resyncRequested && !disposed) {
        resyncRequested = false;
        loadStatus();
      }
    }
  }

  async function startDownload() {
    if (starting || run.running) return;
    starting = true;
    error = '';
    try {
      await api.download.start();
      refreshStatus();
    } catch (e) {
      error = e.message;
    } finally {
      starting = false;
    }
  }

  let stopWarning = $state(true);
  let showStopConfirm = $state(false);

  async function loadConfig() {
    try {
      const data = await api.config.get();
      stopWarning = data.config?.show_stop_warning !== false;
    } catch {
      error = 'Impossibile caricare le impostazioni. Uso valori predefiniti.';
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
    api.download.stop().catch(e => error = e.message);
  }

  // Dettaglio read-only dalle righe home (stesso DetailModal di Gestione,
  // senza azioni: gestire resta compito di Gestione).
  let homeDetailIdx = $state(-1);
  let homeDescriptions = $state({});

  async function openHomeDetail(fileIdx) {
    homeDetailIdx = fileIdx;
    const item = seriesList.find(s => s._file_index === fileIdx);
    if (!item?.path) return;
    try {
      const res = await fetch(BASE + `/api/description?path=${encodeURIComponent(item.path)}`);
      if (res.ok) {
        const data = await res.json();
        if (data.description) homeDescriptions = { ...homeDescriptions, [fileIdx]: data.description };
      }
    } catch {}
  }

  // Lock scroll di sfondo finche un modale home e aperto (dettaglio o
  // conferma stop), con ripristino in cleanup.
  const anyHomeModal = $derived(homeDetailIdx >= 0 || showStopConfirm);
  $effect(() => {
    if (!anyHomeModal) return;
    const prev = document.body.style.overflow;
    document.body.style.overflow = 'hidden';
    return () => { document.body.style.overflow = prev; };
  });

  function connectSse() {
    sse = api.sse();
    sse.onopen = () => { sseConnected = true; refreshStatus(); };
    sse.onmessage = (event) => {
      try {
        const data = JSON.parse(event.data);
        if (!Number.isSafeInteger(data.sequence)) return;
        if (!syncing && data.sequence > run.sequence + 1) loadStatus();
        if (syncing) buffered.push(data);
        else run = applyDownloadEvent(run, data);
        if (data.type === 'done') {
          loadRecentAdded();
          loadRecentlyDownloaded();
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
    disposed = true;
    if (sse) sse.close();
  });
</script>

<div class="vetrina" in:pageEnter|global>
  <OverallHeader
    {downloadRunning}
    {overallStatus}
    {sseConnected}
    {phase}
    {summary}
    {starting}
    onstart={startDownload}
    onstop={stopClicked}
  />

  {#if error}
    <div class="error" role="alert">{error}</div>
  {/if}

  {#if summary}
    <div class="summary-box">
      {#if !run.stopped && !summary.errors}
        <svg class="summary-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" aria-hidden="true"><path d="M22 11.08V12a10 10 0 11-5.93-9.14"/><polyline points="22 4 12 14.01 9 11.01"/></svg>
      {/if}
      <div class="summary-body">
        <span class="summary-title">{run.stopped ? 'Elaborazione interrotta' : summary.errors ? 'Elaborazione terminata con errori' : 'Elaborazione completata'}</span>
        <div class="summary-stats">
          <span class="stat stat-done">{summary.done} completate</span>
          <span class="stat stat-skipped">{summary.skipped} saltate</span>
          <span class="stat stat-error">{summary.errors} errori</span>
          {#if summary.interrupted}<span class="stat stat-error">{summary.interrupted} interrotti</span>{/if}
          <span class="stat stat-total">{summary.total} totali</span>
        </div>
        {#if stats.dlTime > 0 || stats.convTime > 0}
          <div class="summary-times">
            Download: {stats.dlTime.toFixed(1)}s &middot; Conversione: {stats.convTime.toFixed(1)}s
          </div>
        {/if}
      </div>
    </div>
  {/if}

  {#if hasRun}
    <ProgressList
      tasks={taskValues}
      {waitingSeries}
      {seriesList}
      running={downloadRunning}
      {hydration}
    />
  {/if}

    <div class="dashboard-tables">
      <div class="table-col">
        <div class="group-label">Ultime serie aggiunte</div>
        <div class="series-list">
          {#if recentAdded.length > 0}
            {#each recentAdded as s, idx (s._file_index ?? s.name)}
              <RecentSeriesRow item={s} index={idx} onopen={openHomeDetail} />
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
            {#each recentlyDownloaded as s, idx (s._file_index ?? s.name)}
              <RecentSeriesRow item={s} index={idx} onopen={openHomeDetail} />
            {/each}
          {:else}
            <p class="empty-text">Ancora nessun episodio scaricato</p>
          {/if}
        </div>
      </div>
    </div>

  <ConfirmModal
    show={showStopConfirm}
    title="Interrompere il download?"
    message={'Interrompere download e conversioni di questa elaborazione?\n\nLa conservazione dei file parziali segue le impostazioni di ripresa e pulizia.'}
    confirmText="Ferma Download"
    danger={true}
    onConfirm={doStop}
    onCancel={() => showStopConfirm = false}
  />

  {#if homeDetailIdx >= 0}
    {@const hs = seriesList.find(item => item._file_index === homeDetailIdx)}
    {#if hs}
      <DetailModal
        title={hs.name || hs.title}
        poster={posterUrl(hs.path)}
        srcset={posterSrcSet(hs.path)}
        fullPoster={posterUrl(hs.path, 1080)}
        onclose={() => homeDetailIdx = -1}
      >
        {#snippet meta()}
          {#if hs.service}
            <span class="card-service">{hs.service === 'animeU_scraper' ? 'AnimeU' : 'AnimeW'}</span>
          {/if}
          {#if hs.continue}
            <span class="detail-badge">Continua numerazione</span>
          {/if}
          {#if hs.is_high_priority}
            <span class="detail-badge detail-badge-warn">Alta Priorit&agrave;</span>
          {/if}
        {/snippet}
        {#snippet body()}
          {#if homeDescriptions[homeDetailIdx] ?? ''}
            <div class="detail-desc">
              <p>{homeDescriptions[homeDetailIdx]}</p>
            </div>
          {:else}
            <div class="detail-desc detail-desc-empty">Nessuna descrizione disponibile.</div>
          {/if}
        {/snippet}
        {#snippet details()}
          <div class="detail-stats">
            <div class="detail-stat">
              <span class="detail-stat-label">Episodi in locale</span>
              <span class="detail-stat-value">
                {hs.local_episode_count ?? '?'}
                {#if (hs.passed_episodes ?? 0) > 0 && hs.local_episode_count !== hs.passed_episodes}
                  <span class="detail-stat-diff">(config: {hs.passed_episodes})</span>
                {/if}
              </span>
            </div>
            <div class="detail-stat">
              <span class="detail-stat-label">Percorso</span>
              <span class="detail-stat-value detail-stat-path" title={hs.path}>{hs.path || '—'}</span>
            </div>
            <div class="detail-stat">
              <span class="detail-stat-label">URL</span>
              {#if hs.series_page_url || hs.url}
                <a class="detail-stat-value detail-stat-url" href={hs.series_page_url || hs.url} target="_blank" rel="noopener noreferrer" title={hs.series_page_url || hs.url}>{hs.series_page_url || hs.url}</a>
              {:else}
                <span class="detail-stat-value detail-stat-url">—</span>
              {/if}
            </div>
          </div>
        {/snippet}
      </DetailModal>
    {/if}
  {/if}

  <DownloadEventLog events={run.events} />
</div>

<style>
  .vetrina { display: flex; flex-direction: column; gap: 1rem; }

  .error {
    background: var(--danger-bg); border: 1px solid var(--danger-border); color: var(--danger);
    padding: 0.75rem; border-radius: 8px; font-size: 0.85rem;
  }

  .summary-box {
    display: flex; align-items: flex-start; gap: 0.6rem;
    background: var(--bg-secondary); border: 1px solid var(--border-color); border-radius: 8px;
    padding: 0.65rem 0.75rem;
  }
  .summary-icon { width: 18px; height: 18px; color: var(--success); flex-shrink: 0; margin-top: 2px; }
  .summary-body { flex: 1; min-width: 0; }
  .summary-title { font-size: 0.85rem; font-weight: 600; color: var(--text-primary); display: block; margin-bottom: 0.3rem; }
  .summary-stats { display: flex; flex-wrap: wrap; gap: 0.35rem 0.75rem; margin-bottom: 0.2rem; }
  .stat { font-size: 0.8rem; font-weight: 600; }
  .stat-done { color: var(--success); }
  .stat-skipped { color: var(--warning); }
  .stat-error { color: var(--danger); }
  .stat-total { color: var(--text-muted); }
  .summary-times { font-size: 0.78rem; color: var(--text-muted-more); }

  .dashboard-tables {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(min(100%, 320px), 1fr));
    gap: 1.5rem;
  }
  .table-col {
    display: flex;
    flex-direction: column;
    gap: 0.25rem;
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
</style>
