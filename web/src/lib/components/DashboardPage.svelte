<script>
  import { onMount } from 'svelte';
  import { api, BASE, posterUrl } from '../api.js';
  import Dropdown from '../Dropdown.svelte';
  import ConfirmModal from './ConfirmModal.svelte';
  import DirectoryBrowser from './DirectoryBrowser.svelte';

  let series = $state([]);
  let searchQuery = $state('');
  let error = $state('');
  let busy = $state(true);
  let showForm = $state(false);
  let editing = $state(-1);
  let posterError = $state(false);
  let showBrowser = $state(false);
  let descriptions = $state({});
  let detailIndex = $state(-1);
  let sortField = $state('name');
  let sortDir = $state('asc');
  let viewMode = $state('normal');

  let form = $state({
    service: 'animeW_scraper',
    name: '',
    path: '',
    url: '',
    continue: false,
    highPriority: false,
    passedEpisodes: 0,
  });

  let filtered = $derived(
    searchQuery
      ? series.filter(s => (s.name || s.title || '').toLowerCase().includes(searchQuery.toLowerCase()))
      : series
  );

  async function load() {
    busy = true;
    error = '';
    try {
      const opts = sortField && sortField !== 'added' ? { sort: sortField, dir: sortDir } : {};
      let data = (await api.series.list(opts)).series || [];
      if (sortField === 'added' && sortDir === 'desc') data.reverse();
      series = data;
      descriptions = {};
      for (const item of series) {
        loadDescription(item._file_index);
      }
    } catch (e) {
      error = e.message;
    } finally {
      busy = false;
    }
  }

  onMount(() => { load(); });

  let prevSortField = $state(sortField);
  let prevSortDir = $state(sortDir);
  $effect(() => {
    if (sortField !== prevSortField || sortDir !== prevSortDir) {
      prevSortField = sortField;
      prevSortDir = sortDir;
      load();
    }
    sortField, sortDir;
  });

  function setSort(field) {
    if (sortField === field) {
      sortDir = sortDir === 'asc' ? 'desc' : 'asc';
    } else {
      sortField = field;
      const defaultDir = (field === 'local_episode_count' || field === 'continue' || field === 'added') ? 'desc' : 'asc';
      sortDir = defaultDir;
    }
  }

  async function loadDescription(fileIdx) {
    const item = series.find(s => s._file_index === fileIdx);
    if (!item || !item.path) return;
    try {
      const res = await fetch(BASE + `/api/description?path=${encodeURIComponent(item.path)}`);
      if (res.ok) {
        const data = await res.json();
        if (data.description) {
          descriptions = { ...descriptions, [fileIdx]: data.description };
        }
      }
    } catch {}
  }

  function resetForm() {
    form = {
      service: 'animeW_scraper',
      name: '',
      path: '',
      url: '',
      continue: false,
      highPriority: false,
      passedEpisodes: 0,
    };
    posterError = false;
  }

  function openNew() {
    resetForm();
    editing = -1;
    showForm = true;
  }

  function openEdit(fileIdx) {
    const s = series.find(item => item._file_index === fileIdx);
    if (!s) { error = 'Errore: serie con indice ' + fileIdx + ' non trovata.'; return; }
    form = {
      service: s.service || 'animeW_scraper',
      name: s.name || s.title || '',
      path: s.path || '',
      url: s.series_page_url || s.url || '',
      continue: s.continue || false,
      highPriority: s.is_high_priority || false,
      passedEpisodes: s.passed_episodes || 0,
    };
    editing = fileIdx;
    showForm = true;
    posterError = false;
  }

  function closeForm() {
    showForm = false;
    editing = -1;
    resetForm();
  }

  async function saveForm() {
    if (!form.name.trim() || !form.path.trim() || !form.url.trim()) {
      error = 'Nome, Percorso e URL sono obbligatori.';
      return;
    }
    error = '';
    const item = {
      name: form.name.trim(),
      service: form.service,
      path: form.path.trim(),
      series_page_url: form.url.trim(),
      continue: form.continue,
      is_high_priority: form.highPriority,
      passed_episodes: form.passedEpisodes,
      episode_list_selector: '',
      download_link_selector: '',
    };
    try {
      if (editing >= 0) {
        await api.series.update(editing, item);
      } else {
        await api.series.add(item);
      }
      closeForm();
      await load();
    } catch (e) {
      error = e.message;
    }
  }

  let confirmDeleteIdx = $state(-1);
  let confirmDeleteName = $state('');

  function promptRemove(fileIdx) {
    const s = series.find(item => item._file_index === fileIdx);
    confirmDeleteName = s ? (s.name || s.title || 'serie sconosciuta') : 'serie #' + fileIdx;
    confirmDeleteIdx = fileIdx;
  }

  async function doRemove() {
    const idx = confirmDeleteIdx;
    const name = confirmDeleteName;
    confirmDeleteIdx = -1;
    try {
      await api.series.remove(idx);
      if (editing === idx) closeForm();
      if (detailIndex === idx) closeDetail();
      await load();
    } catch (e) {
      error = 'Errore durante l\'eliminazione di "' + name + '": ' + e.message;
    }
  }

  function posterSrc(item) {
    return posterUrl(item ? item.path : '');
  }

  function openDetail(idx) { detailIndex = idx; }
  function closeDetail() { detailIndex = -1; }

  function pickDirectory() {
    showBrowser = true;
  }

  function onBrowserSelect(selectedPath) {
    form.path = selectedPath;
    showBrowser = false;
  }

  function onBrowserCancel() {
    showBrowser = false;
  }

  let showFab = $state(true);
  let lastScrollY = $state(0);

  function handleScroll() {
    const sy = window.scrollY;
    showFab = sy < lastScrollY || sy < 100;
    lastScrollY = sy;
  }

  $effect(() => {
    if (typeof window === 'undefined') return;
    window.addEventListener('scroll', handleScroll, { passive: true });
    return () => window.removeEventListener('scroll', handleScroll);
  });

</script>

<div in:fly={{ y: 8, duration: 200 }}>
<div class="header-row">
  <h2>Gestione Serie</h2>
</div>

<div class="search-row">
  <svg class="search-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="11" cy="11" r="8"/><line x1="21" y1="21" x2="16.65" y2="16.65"/></svg>
  <input type="text" bind:value={searchQuery} placeholder="Cerca per nome..." class="search-input" />
</div>

<div class="sort-row">
  <label class="sort-label" for="sort-field">Ordina</label>
  <Dropdown bind:value={sortField} options={[
    { value: 'name', label: 'Nome' },
    { value: 'added', label: 'Data inserimento' },
    { value: 'local_episode_count', label: 'Episodi' },
    { value: 'continue', label: 'Continua' },
  ]} />
  <button class="btn-icon-only sort-dir-btn" onclick={() => sortDir = sortDir === 'asc' ? 'desc' : 'asc'} title={sortDir === 'asc' ? 'Crescente' : 'Decrescente'}>
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" width="16" height="16">
      {#if sortDir === 'asc'}
        <path d="M12 5v14M8 9l4-4 4 4"/>
      {:else}
        <path d="M12 5v14M8 15l4 4 4-4"/>
      {/if}
    </svg>
  </button>
  <div class="view-toggle">
    <button type="button" class="btn-icon-only" class:active={viewMode === 'normal'} onclick={() => viewMode = 'normal'} title="Vista normale">
      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" width="16" height="16"><rect x="3" y="3" width="7" height="7"/><rect x="14" y="3" width="7" height="7"/><rect x="3" y="14" width="7" height="7"/><rect x="14" y="14" width="7" height="7"/></svg>
    </button>
    <button type="button" class="btn-icon-only" class:active={viewMode === 'compact'} onclick={() => viewMode = 'compact'} title="Vista compatta">
      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" width="16" height="16"><rect x="3" y="3" width="18" height="4"/><rect x="3" y="10" width="18" height="4"/><rect x="3" y="17" width="18" height="4"/></svg>
    </button>
  </div>
</div>

{#if error}
  <div class="error">{error}</div>
{/if}

{#if showForm}
  <div class="modal-overlay" onclick={closeForm} onkeydown={(e) => e.key === 'Escape' && closeForm()}>
    <div class="modal-panel form-modal" onclick={(e) => e.stopPropagation()}>
      <div class="form-modal-body">
        <h3>{editing >= 0 ? 'Modifica: ' + form.name : 'Aggiungi Nuova Serie'}</h3>

        <div class="form-layout">
          <div class="form-poster-col">
            <div class="poster-frame">
              {#if editing >= 0}
                {@const editItem = series.find(item => item._file_index === editing)}
                {#if editItem}
                  <img src={posterSrc(editItem)} alt="poster" class="poster-preview"
                    onerror={() => posterError = true} />
                {/if}
              {:else}
                <div class="poster-placeholder">Nessun<br />Percorso</div>
              {/if}
            </div>
          </div>
          <div class="form-fields-col">
            <div class="field-group">
              <label class="field-label" for="f-service">Servizio di Download</label>
              <div class="radio-row">
                <label class="radio-label">
                  <input type="radio" bind:group={form.service} value="animeW_scraper" />
                  AnimeW Scraper
                </label>
                <label class="radio-label">
                  <input type="radio" bind:group={form.service} value="animeU_scraper" />
                  AnimeU Scraper
                </label>
              </div>
            </div>

            <div class="field-group">
              <label class="field-label" for="f-name">Nome:</label>
              <div class="input-with-btn">
                <input id="f-name" type="text" bind:value={form.name} placeholder="Nome della serie" required />
                <button type="button" class="btn-small-icon" title="Recupera il nome dalla pagina web" onclick={async () => {
                  if (!form.url.trim()) return;
                  try {
                    const result = await api.series.fetchName(form.url);
                    if (result.name) form.name = result.name;
                  } catch (e) {
                    error = 'Impossibile recuperare il nome: ' + e.message;
                  }
                }}>
                  <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" width="14" height="14"><polyline points="23 4 23 10 17 10"/><polyline points="1 20 1 14 7 14"/><path d="M3.51 9a9 9 0 0114.85-3.36L23 10M1 14l4.64 4.36A9 9 0 0020.49 15"/></svg>
                </button>
              </div>
            </div>

            <div class="field-group">
              <label class="field-label" for="f-path">Percorso Cartella:</label>
              <div class="input-with-btn">
                <input id="f-path" type="text" bind:value={form.path} placeholder="/path/to/series" required />
                <button type="button" class="btn-small-icon" onclick={pickDirectory} title="Sfoglia directory">
                  <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" width="14" height="14"><path d="M22 19a2 2 0 01-2 2H4a2 2 0 01-2-2V5a2 2 0 012-2h5l2 3h9a2 2 0 012 2z"/></svg>
                </button>
              </div>
            </div>

            <div class="field-group">
              <label class="field-label" for="f-url">URL Pagina Serie:</label>
              <input id="f-url" type="text" bind:value={form.url} placeholder="https://www.animeworld.ac/play/..." required />
            </div>

            <div class="field-group">
              <label class="checkbox-inline">
                <input type="checkbox" bind:checked={form.continue} />
                Continua numerazione
              </label>
              <label class="checkbox-inline">
                <input type="checkbox" bind:checked={form.highPriority} />
                Alta Priorit&agrave;
              </label>
            </div>

            <div class="field-group">
              <label class="field-label" for="f-passed">Episodi Passati:</label>
              <input id="f-passed" type="number" min="0" max="9999" bind:value={form.passedEpisodes} class="num-input" />
            </div>
          </div>
        </div>

        {#if editing >= 0}
          <div class="form-footer">
            <button class="btn-danger" onclick={() => { const idx = editing; closeForm(); promptRemove(idx); }}>Elimina Serie</button>
            <div class="footer-right">
              <button class="btn-cancel" onclick={closeForm}>Annulla</button>
              <button class="btn-primary" onclick={saveForm}>Salva Modifiche</button>
            </div>
          </div>
        {:else}
          <div class="form-footer">
            <div></div>
            <div class="footer-right">
              <button class="btn-cancel" onclick={closeForm}>Annulla</button>
              <button class="btn-primary" onclick={saveForm}>Salva Modifiche</button>
            </div>
          </div>
        {/if}
      </div>
    </div>
  </div>
{/if}

{#if busy}
  <div class="center"><div class="spinner-sm"></div></div>
{:else if filtered.length === 0}
  <div class="empty">{series.length === 0 ? 'Nessuna serie configurata. Aggiungine una per iniziare!' : 'Nessuna serie corrisponde alla ricerca.'}</div>
{:else}
  <div class="series-grid" class:grid-compact={viewMode === 'compact'}>
    {#each filtered as item, idx (item._file_index)}
      <div class="series-card" style="--i:{idx}" role="button" tabindex="0" onclick={() => openDetail(item._file_index)} onkeydown={(e) => e.key === 'Enter' && openDetail(item._file_index)}>
        <div class="card-poster-wrap">
          <img class="card-poster" src={posterSrc(item)} alt="" loading="lazy" />
          {#if descriptions[item._file_index]}
            <div class="card-desc-overlay">
              <p class="card-desc">{descriptions[item._file_index]}</p>
            </div>
          {/if}
        </div>
        <div class="card-body">
          <h3>{item.name || item.title}</h3>
          <p class="card-path" title={item.path}>{item.path}</p>
          <p class="card-url" title={item.series_page_url || item.url}>{item.series_page_url || item.url}</p>
          <div class="card-meta-row">
            {#if item.service}
              <span class="card-service">{item.service === 'animeU_scraper' ? 'AnimeU' : 'AnimeW'}</span>
            {/if}
            <span class="card-epcount">Ep: {item.local_episode_count ?? '?'}</span>
          </div>
        </div>
        <div class="card-actions">
          <button class="btn-card" onclick={(e) => { e.stopPropagation(); openEdit(item._file_index); }}>
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" width="14" height="14"><path d="M11 4H4a2 2 0 00-2 2v14a2 2 0 002 2h14a2 2 0 002-2v-7"/><path d="M18.5 2.5a2.121 2.121 0 013 3L12 15l-4 1 1-4 9.5-9.5z"/></svg>
            Modifica
          </button>
          <button class="btn-card btn-card-danger" onclick={(e) => { e.stopPropagation(); promptRemove(item._file_index); }}>
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" width="14" height="14"><polyline points="3 6 5 6 21 6"/><path d="M19 6v14a2 2 0 01-2 2H7a2 2 0 01-2-2V6m3 0V4a2 2 0 012-2h4a2 2 0 012 2v2"/><line x1="10" y1="11" x2="10" y2="17"/><line x1="14" y1="11" x2="14" y2="17"/></svg>
            Elimina
          </button>
        </div>
      </div>
    {/each}
  </div>
{/if}

<ConfirmModal
  show={confirmDeleteIdx >= 0}
  title="Eliminare questa serie?"
  message={'Eliminare definitivamente "' + confirmDeleteName + '"?\nQuesta operazione non può essere annullata.'}
  confirmText="Elimina"
  danger={true}
  onConfirm={doRemove}
  onCancel={() => confirmDeleteIdx = -1}
/>

<DirectoryBrowser
  show={showBrowser}
  currentPath={form.path || '~/Video'}
  title="Scegli cartella per la serie"
  onselect={onBrowserSelect}
  oncancel={onBrowserCancel}
/>

{#if detailIndex >= 0}
  {@const s = series.find(item => item._file_index === detailIndex)}
  {#if s}
  <div class="modal-overlay" onclick={closeDetail} onkeydown={(e) => e.key === 'Escape' && closeDetail()}>
    <div class="modal-panel detail-modal" onclick={(e) => e.stopPropagation()}>
      <div class="detail-modal-inner">
        <div class="detail-poster-col">
          <img class="detail-poster" src={posterSrc(s)} alt="" loading="lazy" />
        </div>
        <div class="detail-info-col">
        <button class="detail-close" onclick={closeDetail}>&times;</button>
        <div class="detail-info-scroll">
          <h2 class="detail-title">{s.name || s.title}</h2>
          <div class="detail-meta">
            {#if s.service}
              <span class="card-service">{s.service === 'animeU_scraper' ? 'AnimeU' : 'AnimeW'}</span>
            {/if}
            {#if s.continue}
              <span class="detail-badge">Continua numerazione</span>
            {/if}
            {#if s.is_high_priority}
              <span class="detail-badge detail-badge-warn">Alta Priorit&agrave;</span>
            {/if}
          </div>
          {#if descriptions[detailIndex]}
            <div class="detail-desc">
              <p>{descriptions[detailIndex]}</p>
            </div>
          {:else}
            <div class="detail-desc detail-desc-empty">Nessuna descrizione disponibile.</div>
          {/if}
          <div class="detail-stats">
            <div class="detail-stat">
              <span class="detail-stat-label">Episodi in locale</span>
              <span class="detail-stat-value">
                {s.local_episode_count ?? '?'}
                {#if (s.passed_episodes ?? 0) > 0 && s.local_episode_count !== s.passed_episodes}
                  <span class="detail-stat-diff">(config: {s.passed_episodes})</span>
                {/if}
              </span>
            </div>
            <div class="detail-stat">
              <span class="detail-stat-label">Percorso</span>
              <span class="detail-stat-value detail-stat-path" title={s.path}>{s.path || '—'}</span>
            </div>
            <div class="detail-stat">
              <span class="detail-stat-label">URL</span>
              {#if s.series_page_url || s.url}
                <a class="detail-stat-value detail-stat-url" href={s.series_page_url || s.url} target="_blank" rel="noopener noreferrer" title={s.series_page_url || s.url}>{s.series_page_url || s.url}</a>
              {:else}
                <span class="detail-stat-value detail-stat-url">—</span>
              {/if}
            </div>
          </div>
        </div>
        <div class="detail-actions">
          <button class="btn-primary" onclick={() => { const idx = detailIndex; closeDetail(); openEdit(idx); }}>
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" width="14" height="14"><path d="M11 4H4a2 2 0 00-2 2v14a2 2 0 002 2h14a2 2 0 002-2v-7"/><path d="M18.5 2.5a2.121 2.121 0 013 3L12 15l-4 1 1-4 9.5-9.5z"/></svg>
            Modifica
          </button>
          <button class="btn-danger" onclick={() => { const idx = detailIndex; closeDetail(); promptRemove(idx); }}>
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" width="14" height="14"><polyline points="3 6 5 6 21 6"/><path d="M19 6v14a2 2 0 01-2 2H7a2 2 0 01-2-2V6m3 0V4a2 2 0 012-2h4a2 2 0 012 2v2"/><line x1="10" y1="11" x2="10" y2="17"/><line x1="14" y1="11" x2="14" y2="17"/></svg>
            Elimina
          </button>
        </div>
      </div>
    </div>
  </div>
  </div>
  {:else}
    <div class="error">Serie con ID {detailIndex} non trovata. Potrebbe essere stata eliminata.</div>
  {/if}
{/if}

{#if showFab && !showForm && detailIndex < 0}
  <button class="fab" onclick={openNew} aria-label="Aggiungi Serie">
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" width="28" height="28"><line x1="12" y1="5" x2="12" y2="19"/><line x1="5" y1="12" x2="19" y2="12"/></svg>
  </button>
{/if}

</div>

<style>
  .header-row { display: flex; justify-content: space-between; align-items: center; margin-bottom: 1rem; }
  h2 { font-size: 1.5rem; font-weight: 700; }
  .btn-icon-only {
    background: none; border: 1px solid var(--border-color); border-radius: 8px;
    color: var(--text-secondary); cursor: pointer; padding: 0.5rem;
    display: flex; align-items: center; justify-content: center;
  }
  .btn-icon-only:hover { background: var(--bg-tertiary); color: var(--text-primary); }
  .btn-primary {
    padding: 0.55rem 1.1rem; background: var(--accent); border: none; border-radius: 8px;
    color: #fff; font-size: 0.85rem; font-weight: 600; cursor: pointer;
    display: flex; align-items: center; gap: 0.4rem;
  }
  .btn-primary:hover { background: var(--accent-hover); }
  .btn-cancel {
    padding: 0.55rem 1.1rem; background: none; border: 1px solid var(--btn-cancel-border); border-radius: 8px;
    color: var(--text-secondary); font-size: 0.85rem; cursor: pointer;
  }
  .btn-cancel:hover { background: var(--bg-tertiary); color: var(--text-primary); }
  .btn-danger {
    padding: 0.55rem 1.1rem; background: var(--danger); border: none; border-radius: 8px;
    color: #fff; font-size: 0.85rem; font-weight: 600; cursor: pointer;
    display: flex; align-items: center; gap: 0.4rem;
  }
  .btn-danger:hover { opacity: 0.85; }

  .search-row {
    display: flex; align-items: center; gap: 0.5rem;
    background: var(--bg-secondary); border: 1px solid var(--border-color); border-radius: 8px;
    padding: 0.5rem 0.75rem; margin-bottom: 1rem;
  }
  .search-icon { width: 16px; height: 16px; color: var(--text-muted); flex-shrink: 0; }
  .search-input {
    background: none; border: none; color: var(--text-primary); font-size: 0.9rem;
    width: 100%; outline: none;
  }
  .search-input::placeholder { color: var(--text-muted-more); }

  .sort-row {
    display: flex; align-items: center; gap: 0.5rem;
    margin-bottom: 1rem;
  }
  .sort-label { font-size: 0.8rem; color: var(--text-muted); }
  .sort-dir-btn { padding: 0.35rem; }
  .view-toggle { display: flex; gap: 0.15rem; margin-left: auto; }
  .view-toggle .btn-icon-only { padding: 0.35rem; border-radius: 6px; }
  .view-toggle .btn-icon-only.active { background: var(--accent); color: #fff; border-color: var(--accent); }

  .error {
    background: var(--danger-bg); border: 1px solid var(--danger-border); color: var(--danger);
    padding: 0.75rem; border-radius: 8px; margin-bottom: 1rem; font-size: 0.85rem;
  }
  .center { display: flex; justify-content: center; padding: 2rem; }
  .spinner-sm {
    width: 24px; height: 24px; border: 2px solid var(--border-color); border-top-color: var(--accent);
    border-radius: 50%; animation: spin 0.8s linear infinite;
  }
  @keyframes spin { to { transform: rotate(360deg); } }
  .empty { text-align: center; padding: 3rem; color: var(--text-muted); font-size: 0.9rem; }

  .series-grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(220px, 1fr)); gap: 1rem; }
  .grid-compact { grid-template-columns: repeat(auto-fill, minmax(140px, 1fr)); gap: 0.75rem; }
  .grid-compact .card-poster { aspect-ratio: 2/3; }
  .grid-compact .card-body h3 { font-size: 0.78rem; }
  .grid-compact .card-path { display: none; }
  .grid-compact .card-url { display: none; }
  .grid-compact .card-service { font-size: 0.62rem; }
  .grid-compact .card-epcount { font-size: 0.62rem; }
  .grid-compact .card-actions .btn-card { padding: 0.4rem; font-size: 0.72rem; gap: 0.2rem; }
  .grid-compact .btn-card svg { width: 12px; height: 12px; }
  .series-card {
    background: var(--bg-secondary); border: 1px solid var(--border-color); border-radius: 12px; overflow: hidden;
    display: flex; flex-direction: column;
    transition: transform 0.2s ease, box-shadow 0.2s ease;
    animation: cardIn 0.35s ease-out both;
    animation-delay: calc(var(--i, 0) * 40ms);
  }
  @media (hover: hover) {
    .series-card:hover {
      transform: scale(1);
      box-shadow: 0 8px 30px rgba(0,0,0,0.35);
      z-index: 2;
    }
  }
  .card-poster-wrap {
    position: relative;
    overflow: hidden;
    cursor: pointer;
  }
  .card-poster {
    width: 100%; aspect-ratio: 2 / 3; object-fit: cover;
    background: var(--bg-tertiary);
    display: block;
    transform: translateZ(0);
  }
  .card-desc-overlay {
    position: absolute;
    inset: 0;
    background: linear-gradient(to top, rgba(0,0,0,0.92) 0%, rgba(0,0,0,0.6) 40%, rgba(0,0,0,0.2) 100%);
    display: flex;
    align-items: flex-end;
    padding: 0.75rem;
    opacity: 0;
    transition: opacity 0.25s ease;
  }
  @media (hover: hover) {
    .series-card:hover .card-desc-overlay {
      opacity: 1;
    }
  }
  .card-desc {
    color: #e0e0e0;
    font-size: 0.78rem;
    line-height: 1.45;
    display: -webkit-box; -webkit-line-clamp: 8; -webkit-box-orient: vertical;
    overflow: hidden;
  }
  .card-body { padding: 0.6rem 0.75rem; flex: 1; }
  .card-body h3 { font-size: 0.9rem; font-weight: 600; margin-bottom: 0.25rem; color: var(--text-primary); }
  .card-path { font-size: 0.72rem; color: var(--text-muted-more); white-space: nowrap; overflow: hidden; text-overflow: ellipsis; margin-bottom: 0.15rem; }
  .card-url { font-size: 0.7rem; color: var(--text-muted-more); white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
  .card-meta-row { display: flex; align-items: center; gap: 0.4rem; margin-top: 0.3rem; }
  .card-service {
    display: inline-block; font-size: 0.68rem; color: var(--accent);
    background: var(--card-service-bg); padding: 0.12rem 0.45rem; border-radius: 4px;
  }
  .card-epcount { font-size: 0.68rem; color: var(--text-muted); }
  .card-actions {
    display: flex; gap: 0; border-top: 1px solid var(--border-color);
  }
  .btn-card {
    flex: 1; display: flex; align-items: center; justify-content: center; gap: 0.3rem;
    background: none; border: none; color: var(--accent); padding: 0.55rem;
    cursor: pointer; font-size: 0.8rem; transition: all 0.1s;
  }
  .btn-card:hover { background: var(--bg-tertiary); }
  .btn-card + .btn-card { border-left: 1px solid var(--border-color); }
  .btn-card-danger { color: var(--danger); }
  .btn-card-danger:hover { background: var(--danger-bg); }

  .form-modal {
    animation: scaleIn 0.25s ease-out;
    background: var(--bg-secondary); border: 1px solid var(--border-color); border-radius: 16px;
    padding: 1.5rem; min-width: 520px; max-width: 90vw;
    max-height: 90vh; overflow-y: auto;
  }
  .form-modal-body h3 { font-size: 1.1rem; font-weight: 700; margin-bottom: 1rem; color: var(--accent-light); }

  .form-layout { display: flex; gap: 1.25rem; }
  .form-poster-col { flex-shrink: 0; }
  .poster-frame {
    width: 140px; aspect-ratio: 2 / 3; border: 1px solid var(--border-color); border-radius: 6px;
    background: var(--poster-frame-bg); display: flex; align-items: center; justify-content: center;
    overflow: hidden;
  }
  .poster-preview { width: 100%; height: 100%; object-fit: cover; transform: translateZ(0); }
  .poster-placeholder { color: var(--text-muted-more); font-size: 0.8rem; text-align: center; line-height: 1.5; }
  .form-fields-col { flex: 1; min-width: 0; display: flex; flex-direction: column; gap: 0.75rem; }

  .field-group { }
  .field-label { display: block; font-size: 0.8rem; color: var(--text-secondary); margin-bottom: 0.25rem; }
  .field-group input[type="text"], .field-group input[type="number"] {
    width: 100%; padding: 0.55rem 0.75rem; background: var(--bg-tertiary); border: 1px solid var(--border-color);
    border-radius: 6px; color: var(--text-primary); font-size: 0.85rem;
  }
  .field-group input:focus { outline: none; border-color: var(--accent); }
  .num-input { max-width: 100px; }
  .input-with-btn { display: flex; gap: 0.3rem; }
  .input-with-btn input { flex: 1; }
  .btn-small-icon {
    padding: 0.45rem 0.5rem; background: var(--bg-tertiary); border: 1px solid var(--border-color);
    border-radius: 6px; color: var(--text-muted); cursor: pointer; display: flex; align-items: center;
    flex-shrink: 0;
  }
  .btn-small-icon:hover { background: var(--bg-hover); color: var(--text-primary); }
  .btn-small-icon:disabled { cursor: not-allowed; opacity: 0.5; }

  .radio-row { display: flex; gap: 1rem; }
  .radio-label {
    display: flex; align-items: center; gap: 0.35rem;
    font-size: 0.85rem; color: var(--text-secondary); cursor: pointer;
  }
  .radio-label input[type="radio"] { accent-color: var(--accent); margin: 0; }
  .checkbox-inline {
    display: flex; align-items: center; gap: 0.35rem;
    font-size: 0.85rem; color: var(--text-secondary); cursor: pointer; margin-right: 1rem;
  }
  .checkbox-inline input[type="checkbox"] { accent-color: var(--accent); margin: 0; }

  .form-footer {
    display: flex; justify-content: space-between; align-items: center;
    padding-top: 1rem; margin-top: 0.5rem; border-top: 1px solid var(--border-color);
  }
  .footer-right { display: flex; gap: 0.5rem; align-items: center; }




  @keyframes cardIn {
    from { opacity: 0; transform: translateY(12px); }
    to { opacity: 1; transform: translateY(0); }
  }
  @keyframes fadeIn {
    from { opacity: 0; }
    to { opacity: 1; }
  }
  @keyframes scaleIn {
    from { opacity: 0; transform: scale(0.95); }
    to { opacity: 1; transform: scale(1); }
  }
  @keyframes slideUp {
    from { opacity: 0; transform: translateY(10px); }
    to { opacity: 1; transform: translateY(0); }
  }

  .modal-overlay {
    position: fixed; top: 0; left: 0; right: 0; bottom: 0;
    display: flex; align-items: center; justify-content: center;
    background: var(--overlay); z-index: 200;
    border: none; padding: 0; cursor: default;
    animation: fadeIn 0.2s ease;
  }
  .detail-modal {
    animation: scaleIn 0.25s ease-out;
    max-width: 90vw; max-height: 90vh;
  }
  .detail-modal-inner {
    display: flex; gap: 0;
    background: var(--bg-secondary); border: 1px solid var(--border-color);
    border-radius: 16px; overflow: hidden;
    max-height: 80vh;
  }
  .detail-poster-col {
    flex-shrink: 0;
  }
  .detail-poster {
    width: 260px; height: 100%; aspect-ratio: 2/3; object-fit: cover;
    display: block;
    transform: translateZ(0);
  }
  .detail-info-col {
    flex: 1; min-width: 0; padding: 1.5rem;
    display: flex; flex-direction: column;
    position: relative; max-width: 460px;
  }
  .detail-info-scroll {
    flex: 1; overflow-y: auto;
    display: flex; flex-direction: column; gap: 0.75rem;
  }
  .detail-close {
    position: absolute; top: 0.75rem; right: 0.75rem;
    background: none; border: none; color: var(--text-muted);
    font-size: 1.5rem; cursor: pointer; line-height: 1;
    padding: 0.25rem;
  }
  .detail-close:hover { color: var(--text-primary); }
  .detail-title { font-size: 1.15rem; font-weight: 700; color: var(--text-primary); padding-right: 2rem; }
  .detail-meta { display: flex; flex-wrap: wrap; gap: 0.4rem; align-items: center; }
  .detail-badge {
    font-size: 0.72rem; color: var(--success); background: var(--success-border);
    padding: 0.12rem 0.5rem; border-radius: 4px; font-weight: 600;
  }
  .detail-badge-warn { color: var(--warning-text); background: var(--warning-bg); }
  .detail-desc {
    font-size: 0.85rem; color: var(--text-secondary); line-height: 1.6;
    max-height: 200px; overflow-y: auto;
    animation: slideUp 0.3s ease-out;
  }
  .detail-desc-empty { color: var(--text-muted); font-style: italic; }
  .detail-stats { display: flex; flex-direction: column; gap: 0.5rem; }
  .detail-stat { display: flex; justify-content: space-between; align-items: center; }
  .detail-stat-label { font-size: 0.75rem; color: var(--text-muted); }
  .detail-stat-value { font-size: 0.85rem; color: var(--text-primary); font-weight: 600; }
  .detail-stat-path { font-size: 0.78rem; word-break: break-all; text-align: right; max-width: 55%; }
  .detail-stat-url { font-size: 0.75rem; font-weight: 400; word-break: break-all; text-align: right; max-width: 55%; }
  .detail-stat-url:hover { color: var(--accent); }
  .detail-actions { display: flex; gap: 0.5rem; padding-top: 0.75rem; border-top: 1px solid var(--border-color); flex-shrink: 0;
    padding-bottom: 96px;
  }
  .detail-stat-diff { color: var(--text-muted); font-size: 0.75rem; margin-left: 0.4rem; }

  .fab {
    position: fixed; right: 1.5rem;
    width: 56px; height: 56px; border-radius: 50%;
    background: var(--accent); border: none; color: #fff;
    display: flex; align-items: center; justify-content: center;
    cursor: pointer; z-index: 150;
    box-shadow: 0 4px 16px rgba(0,0,0,0.35);
    transition: transform 0.2s ease, opacity 0.2s ease, background 0.2s ease;
    bottom: 112px;
  }
  .fab:hover { background: var(--accent-hover); transform: scale(1.08); }
  .fab:active { transform: scale(0.95); }

  @media (max-width: 768px) {
    .series-grid { grid-template-columns: 1fr; }
    .series-grid.grid-compact { grid-template-columns: repeat(2, 1fr); }
    .grid-compact .card-poster { max-height: 180px; }
    .series-card:hover { transform: none; box-shadow: none; }
    .card-desc-overlay {
      background: linear-gradient(to top, rgba(0,0,0,0.95) 0%, rgba(0,0,0,0.7) 50%, rgba(0,0,0,0.3) 100%);
    }
    .form-modal {
      position: fixed; top: 0; left: 0; right: 0; bottom: 0;
      border-radius: 0; min-width: auto;
      max-width: 100vw; max-height: 100vh;
      padding-bottom: 96px;
    }
    .form-layout { flex-direction: column; }
    .form-poster-col { display: none; }
    .form-modal-body h3 { font-size: 1rem; }
    .header-row { flex-wrap: wrap; gap: 0.5rem; }
    .btn-primary { padding: 0.45rem 0.85rem; font-size: 0.8rem; }
    .detail-modal {
      position: fixed; top: 0; left: 0; right: 0; bottom: 0;
      max-width: 100vw; max-height: 100dvh; border-radius: 0;
      animation: fadeIn 0.2s ease;
    }
    .detail-modal-inner {
      flex-direction: column; border-radius: 0;
      max-height: 100dvh; height: 100dvh;
    }
    .detail-poster { width: 100%; aspect-ratio: 2/3; max-height: 40dvh; object-fit: cover; }
    .detail-info-col { max-width: none; padding: 1rem; }
    .detail-desc { max-height: none; }
    .detail-stat-path { max-width: none; }
    .detail-stat-url { max-width: none; }
  }

  @media (orientation: landscape) and (max-height: 520px) {
    .detail-modal {
      position: fixed; top: 0; left: 0; right: 0; bottom: 0;
      max-width: 100vw; max-height: 100dvh; border-radius: 0;
    }
    .detail-modal-inner {
      flex-direction: row; border-radius: 0;
      max-height: 100dvh; height: 100dvh;
    }
    .detail-poster { width: 160px; max-height: 100dvh; aspect-ratio: 2/3; object-fit: cover; }
    .detail-info-col { max-width: none; padding: 0.75rem; }
    .detail-actions { padding-bottom: 48px; }
    .form-modal {
      position: fixed; top: 0; left: 0; right: 0; bottom: 0;
      border-radius: 0; min-width: auto;
      max-width: 100vw; max-height: 100vh;
      padding-bottom: 48px;
    }
  }
</style>
