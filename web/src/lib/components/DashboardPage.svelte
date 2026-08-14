<script>
  import { onMount } from 'svelte';
  import { api, BASE, posterUrl } from '../api.js';
  import Dropdown from '../Dropdown.svelte';
  import ConfirmModal from './ConfirmModal.svelte';
  import DirectoryBrowser from './DirectoryBrowser.svelte';
  import SeriesList from './SeriesList.svelte';
  import DetailModal from './DetailModal.svelte';
  import AddSeriesForm from './AddSeriesForm.svelte';

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

{#if busy}
  <div class="center"><div class="spinner-sm"></div></div>
{:else}
  <SeriesList
    items={filtered}
    viewMode={viewMode}
    totalCount={series.length}
    descriptions={descriptions}
    onopen={openDetail}
    onedit={openEdit}
    onremove={promptRemove}
  />
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

{#if showForm}
  <AddSeriesForm
    show={showForm}
    {form}
    {editing}
    {series}
    onclose={closeForm}
    onsave={saveForm}
    ondelete={() => { const idx = editing; closeForm(); promptRemove(idx); }}
    onpick={pickDirectory}
    onerror={(msg) => error = msg}
  />
{/if}

{#if detailIndex >= 0}
  {@const s = series.find(item => item._file_index === detailIndex)}
  {#if s}
    <DetailModal
      series={s}
      description={descriptions[detailIndex]}
      poster={posterSrc(s)}
      onclose={closeDetail}
      onedit={() => { const idx = detailIndex; closeDetail(); openEdit(idx); }}
      onremove={() => { const idx = detailIndex; closeDetail(); promptRemove(idx); }}
    />
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
    .header-row { flex-wrap: wrap; gap: 0.5rem; }
  }
</style>
