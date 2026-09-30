<script>
  import { api, BASE } from '../api.js';
  import ConfirmModal from './ConfirmModal.svelte';

  let { show = false, currentPath = '~/Video', title = 'Sfoglia Directory', onselect, oncancel } = $props();

  const MAX_PINS = 20;

  let path = $state('');
  let parent = $state(null);
  let entries = $state([]);
  let loading = $state(false);
  let error = $state('');
  let manualInput = $state('');
  let sortMode = $state('name');
  let search = $state('');

  let mounts = $state([]);
  let pinned = $state([]);

  // Creazione: null | 'dir' | 'file'
  let creating = $state(null);
  let newName = $state('');
  let createError = $state('');

  // Rimozione: percorso in attesa di conferma
  let deleteTarget = $state(null);
  let deleteRecursive = $state(false);
  let busyDelete = $state(false);

  function formatMtime(mtime) {
    if (!mtime) return '';
    const d = new Date(mtime * 1000);
    const dd = String(d.getDate()).padStart(2, '0');
    const mm = String(d.getMonth() + 1).padStart(2, '0');
    const yyyy = d.getFullYear();
    const hh = String(d.getHours()).padStart(2, '0');
    const mi = String(d.getMinutes()).padStart(2, '0');
    return dd + '/' + mm + '/' + yyyy + ' ' + hh + ':' + mi;
  }

  // Filtro locale: nessun endpoint di ricerca, il match e' sui nomi della
  // cartella corrente (case-insensitive).
  let filtered = $derived.by(() => {
    const q = search.trim().toLowerCase();
    if (!q) return entries;
    return entries.filter((e) => e.name.toLowerCase().includes(q));
  });

  let sorted = $derived.by(() => {
    if (sortMode === 'date') {
      return [...filtered].sort((a, b) => ((b.mtime || 0) - (a.mtime || 0)));
    }
    return filtered;
  });

  async function load(pathToLoad) {
    loading = true;
    error = '';
    try {
      const data = await api.browse.list(pathToLoad, true);
      entries = data.entries || [];
      path = data.path || pathToLoad;
      parent = data.parent ?? null;
    } catch (e) {
      entries = [];
      error = e.message || 'Errore nel caricamento';
    } finally {
      loading = false;
    }
  }

  // Dischi e preferiti sono indipendenti dalla cartella corrente: si caricano
  // una volta all'apertura (e su refresh esplicito).
  async function loadSidebars() {
    try {
      const data = await api.browse.mounts();
      mounts = data.mounts || [];
    } catch {
      mounts = [];
    }
    try {
      const data = await api.config.get();
      pinned = Array.isArray(data.config?.pinned_paths) ? data.config.pinned_paths : [];
    } catch {
      pinned = [];
    }
  }

  // Il parent arriva dal server: niente splitting del path, quindi funziona
  // anche su Windows (separatore '\') e con path che contengono spazi.
  function goUp() {
    if (parent) {
      load(parent);
      return;
    }
    if (!parent && path === '/') return;
    // Fallback: nessun parent dal server (root), resta fermo.
  }

  function handleKeydown(e) {
    if (e.key === 'Escape' && oncancel) oncancel();
  }

  function handleManualBrowse() {
    if (manualInput.trim()) load(manualInput.trim());
  }

  function handleSelect() {
    if (onselect) onselect(path);
  }

  function isPinned(p) {
    return pinned.includes(p);
  }

  async function togglePin() {
    if (!path) return;
    const next = isPinned(path) ? pinned.filter((p) => p !== path) : [path, ...pinned].slice(0, MAX_PINS);
    pinned = next;
    try {
      await api.config.set({ pinned_paths: next });
    } catch (e) {
      pinned = pinned.includes(path) ? pinned.filter((p) => p !== path) : [path, ...pinned].slice(0, MAX_PINS);
      error = 'Salvataggio preferiti fallito: ' + e.message;
    }
  }

  // ---- Creazione ----
  function startCreate(kind) {
    creating = kind;
    newName = '';
    createError = '';
  }

  function cancelCreate() {
    creating = null;
    newName = '';
    createError = '';
  }

  async function submitCreate() {
    const name = newName.trim();
    if (!name) {
      createError = 'Nome non valido';
      return;
    }
    // Stessa validazione del server, per non fare un roundtrip inutile.
    if (name === '.' || name === '..' || name.includes('/') || name.includes('\\')) {
      createError = 'Nome non valido';
      return;
    }
    const call = creating === 'dir' ? api.browse.mkdir(path, name) : api.browse.touch(path, name);
    try {
      await call;
      cancelCreate();
      await load(path);
    } catch (e) {
      createError = e.message;
    }
  }

  // ---- Rimozione (doppia conferma quando la cartella non e' vuota) ----
  function askDelete(entry) {
    deleteTarget = entry;
    deleteRecursive = false;
    error = '';
  }

  function cancelDelete() {
    deleteTarget = null;
    deleteRecursive = false;
  }

  async function confirmDelete() {
    if (!deleteTarget) return;
    const target = deleteTarget;
    if (busyDelete) return;
    busyDelete = true;
    try {
      await api.browse.remove(target.path, deleteRecursive);
      cancelDelete();
      await load(path);
    } catch (e) {
      if (e.status === 409) {
        // Cartella non vuota: secondo passaggio, con il conteggio elementi.
        const count = e.count;
        deleteRecursive = true;
        error = 'La cartella non è vuota' + (count ? ' (' + count + ' elementi)' : '') +
                ': conferma di nuovo per eliminare tutto.';
      } else {
        error = e.message;
        cancelDelete();
      }
    } finally {
      busyDelete = false;
    }
  }

  $effect(() => {
    if (show && currentPath) {
      path = currentPath;
      manualInput = currentPath;
      search = '';
      cancelCreate();
      cancelDelete();
      error = '';
      load(currentPath);
      loadSidebars();
    }
  });
</script>

{#if show}
  <div class="browser-overlay" onclick={oncancel} onkeydown={handleKeydown} role="dialog" aria-modal="true" tabindex="-1">
    <div class="browser-modal" onclick={(e) => e.stopPropagation()} onkeydown={(e) => e.stopPropagation()} role="presentation">
      <div class="browser-header">
        <h3>{title}</h3>
      </div>

      <div class="browser-path-bar">
        <span class="browser-path" title={path}>{path}</span>
        <button type="button" class="btn-icon-sm" onclick={goUp} title="Cartella superiore" disabled={!parent}>
          <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" width="20" height="20"><path d="M5 12h14M12 5l-7 7 7 7"/></svg>
        </button>
        <button type="button" class="btn-icon-sm" class:starred={isPinned(path)} onclick={togglePin}
                title={isPinned(path) ? 'Rimuovi dai preferiti' : 'Aggiungi ai preferiti'} aria-label="Preferito">
          {#if isPinned(path)}
            <svg viewBox="0 0 24 24" fill="currentColor" width="18" height="18"><path d="M12 2l2.9 6.3 6.9.8-5.1 4.7 1.4 6.8L12 17.3 5.9 20.6l1.4-6.8L2.2 9.1l6.9-.8z"/></svg>
          {:else}
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" width="18" height="18"><path d="M12 2l2.9 6.3 6.9.8-5.1 4.7 1.4 6.8L12 17.3 5.9 20.6l1.4-6.8L2.2 9.1l6.9-.8z"/></svg>
          {/if}
        </button>
      </div>

      <div class="browser-manual-row">
        <input type="text" bind:value={manualInput} placeholder="Inserisci percorso manualmente..." />
        <button type="button" class="btn-small" onclick={handleManualBrowse}>Vai</button>
      </div>

      <div class="browser-search-row">
        <svg class="search-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" width="15" height="15"><circle cx="11" cy="11" r="7"/><path d="M21 21l-4.3-4.3"/></svg>
        <input type="text" bind:value={search} placeholder="Cerca in questa cartella..." aria-label="Cerca" />
        {#if search}
          <button type="button" class="btn-icon-sm" onclick={() => search = ''} title="Azzera ricerca" aria-label="Azzera ricerca">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" width="15" height="15"><path d="M18 6L6 18M6 6l12 12"/></svg>
          </button>
        {/if}
      </div>

      <div class="browser-sort-bar">
        <span class="sort-label">Ordina:</span>
        <button type="button" class="btn-sort" class:active={sortMode === 'name'} onclick={() => sortMode = 'name'}>Nome</button>
        <button type="button" class="btn-sort" class:active={sortMode === 'date'} onclick={() => sortMode = 'date'}>Data</button>
        <span class="sort-spacer"></span>
        <button type="button" class="btn-sort" onclick={() => startCreate('dir')}>+ Cartella</button>
        <button type="button" class="btn-sort" onclick={() => startCreate('file')}>+ File</button>
      </div>

      {#if creating}
        <div class="browser-create-row">
          <input type="text" bind:value={newName}
                 placeholder={creating === 'dir' ? 'Nome della cartella' : 'Nome del file'}
                 aria-label="Nuovo nome"
                 onkeydown={(e) => e.key === 'Enter' && submitCreate()} />
          <button type="button" class="btn-small" onclick={submitCreate}>Crea</button>
          <button type="button" class="btn-cancel-sm" onclick={cancelCreate}>Annulla</button>
          {#if createError}<span class="create-error">{createError}</span>{/if}
        </div>
      {/if}

      <div class="browser-list">
        {#if loading}
          <div class="browser-center"><div class="spinner-sm"></div></div>
        {:else if error}
          <div class="browser-error">{error}</div>
        {:else if sorted.length === 0 && mounts.length === 0 && pinned.length === 0 && !search.trim()}
          <div class="browser-empty">Nessuna sottodirectory</div>
        {:else}
          {#if parent}
            <div class="browser-entry" onclick={goUp} role="button" tabindex="0" onkeydown={(e) => e.key === 'Enter' && goUp()}>
              <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" width="22" height="22"><path d="M5 12h14M12 5l-7 7 7 7"/></svg>
              <span class="entry-name">.. (su)</span>
            </div>
          {/if}

          {#if pinned.length > 0}
            <div class="browser-section">Preferiti</div>
            {#each pinned as p (p)}
              <div class="browser-entry side-entry" onclick={() => load(p)} role="button" tabindex="0"
                   onkeydown={(e) => e.key === 'Enter' && load(p)}>
                <svg viewBox="0 0 24 24" fill="currentColor" width="20" height="20" class="pin-icon"><path d="M12 2l2.9 6.3 6.9.8-5.1 4.7 1.4 6.8L12 17.3 5.9 20.6l1.4-6.8L2.2 9.1l6.9-.8z"/></svg>
                <span class="entry-name">{p}</span>
              </div>
            {/each}
          {/if}

          {#if mounts.length > 0}
            <div class="browser-section">Dischi</div>
            {#each mounts as m (m.path)}
              <div class="browser-entry side-entry" onclick={() => load(m.path)} role="button" tabindex="0"
                   onkeydown={(e) => e.key === 'Enter' && load(m.path)}>
                <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" width="20" height="20" class="mount-icon">
                  <rect x="2" y="5" width="20" height="14" rx="2"/><path d="M2 10h20"/><path d="M6 15h4"/>
                </svg>
                <span class="entry-name">{m.name}</span>
                <span class="entry-path">{m.path}</span>
              </div>
            {/each}
          {/if}

          <div class="browser-section">Contenuto</div>
          {#if sorted.length === 0}
            <div class="browser-empty">{search.trim() ? 'Nessun elemento corrisponde alla ricerca' : 'Cartella vuota'}</div>
          {:else}
            {#each sorted as entry (entry.path)}
              <div class="browser-entry" role="button" tabindex="0"
                   onclick={() => entry.type === 'dir' && load(entry.path)}
                   onkeydown={(e) => e.key === 'Enter' && entry.type === 'dir' && load(entry.path)}>
                {#if entry.type === 'dir'}
                  <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" width="22" height="22"><path d="M22 19a2 2 0 01-2 2H4a2 2 0 01-2-2V5a2 2 0 012-2h5l2 3h9a2 2 0 012 2z"/></svg>
                {:else}
                  <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" width="22" height="22"><path d="M14 2H6a2 2 0 00-2 2v16a2 2 0 002 2h12a2 2 0 002-2V8z"/><path d="M14 2v6h6"/></svg>
                {/if}
                <span class="entry-name">{entry.name}</span>
                {#if sortMode === 'date' && entry.mtime}
                  <span class="entry-date">{formatMtime(entry.mtime)}</span>
                {/if}
                <button type="button" class="btn-icon-sm entry-del" title="Elimina"
                        aria-label={'Elimina ' + entry.name}
                        onclick={(e) => { e.stopPropagation(); askDelete(entry); }}>
                  <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" width="16" height="16"><path d="M3 6h18M8 6V4h8v2M19 6l-1 14H6L5 6"/></svg>
                </button>
              </div>
            {/each}
          {/if}
        {/if}
      </div>

      <div class="browser-footer">
        <button type="button" class="btn-cancel" onclick={oncancel}>Annulla</button>
        <button type="button" class="btn-primary" onclick={handleSelect}>Seleziona questa cartella</button>
      </div>
    </div>

    <ConfirmModal
      show={deleteTarget !== null && !deleteRecursive}
      title="Eliminare?"
      message={'Eliminare "' + (deleteTarget?.name ?? '') + '"?\nQuesta operazione non può essere annullata.'}
      confirmText="Elimina"
      danger={true}
      onConfirm={confirmDelete}
      onCancel={cancelDelete}
    />

    <ConfirmModal
      show={deleteTarget !== null && deleteRecursive}
      title="Eliminare tutto il contenuto?"
      message={'"' + (deleteTarget?.name ?? '') + '" non è vuota.\nEliminare ricorsivamente tutto il contenuto? L\'operazione non può essere annullata.'}
      confirmText="Elimina tutto"
      danger={true}
      onConfirm={confirmDelete}
      onCancel={cancelDelete}
    />
  </div>
{/if}

<style>
  .browser-overlay {
    position: fixed; top: 0; left: 0; right: 0; bottom: 0;
    background: var(--overlay); z-index: 400;
    display: flex; align-items: center; justify-content: center;
    animation: fadeIn 0.15s ease;
    border: none; padding: 0; cursor: default;
  }
  .browser-modal {
    background: var(--bg-secondary); border: 1px solid var(--border-color);
    border-radius: 12px; padding: 1.25rem;
    min-width: 480px; max-width: 90vw;
    max-height: 80vh; display: flex; flex-direction: column;
    animation: scaleIn 0.2s ease-out;
    box-shadow: 0 12px 40px rgba(0,0,0,0.5);
  }
  .browser-header { margin-bottom: 0.5rem; }
  .browser-header h3 { font-size: 1rem; font-weight: 700; color: var(--text-primary); }
  .browser-path-bar {
    display: flex; align-items: center; gap: 0.5rem;
    background: var(--bg-tertiary); border-radius: 6px;
    padding: 0.5rem 0.7rem; margin-bottom: 0.5rem;
  }
  .browser-path {
    flex: 1; font-size: 0.8rem; color: var(--text-muted);
    font-family: monospace; overflow: hidden; text-overflow: ellipsis; white-space: nowrap;
  }
  .btn-icon-sm {
    background: none; border: none; color: var(--text-muted); cursor: pointer;
    padding: 0.25rem; display: flex; border-radius: 4px; flex-shrink: 0;
  }
  .btn-icon-sm:hover:not(:disabled) { background: var(--bg-hover); color: var(--text-primary); }
  .btn-icon-sm:disabled { opacity: 0.4; cursor: default; }
  .btn-icon-sm.starred { color: #f5a623; }
  .browser-manual-row {
    display: flex; gap: 0.4rem; margin-bottom: 0.5rem;
  }
  .browser-manual-row input {
    flex: 1; padding: 0.45rem 0.7rem; font-size: 0.8rem; font-family: monospace;
    background: var(--bg-tertiary); border: 1px solid var(--border-color);
    border-radius: 6px; color: var(--text-primary); outline: none;
  }
  .browser-manual-row input:focus { border-color: var(--accent); }
  .browser-search-row {
    display: flex; align-items: center; gap: 0.4rem; margin-bottom: 0.5rem;
    background: var(--bg-tertiary); border: 1px solid var(--border-color);
    border-radius: 6px; padding: 0.3rem 0.6rem;
  }
  .browser-search-row:focus-within { border-color: var(--accent); }
  .search-icon { color: var(--text-muted); flex-shrink: 0; }
  .browser-search-row input {
    flex: 1; padding: 0.3rem 0; font-size: 0.8rem;
    background: none; border: none; color: var(--text-primary); outline: none;
  }
  .btn-small {
    padding: 0.45rem 0.85rem; font-size: 0.8rem;
    background: var(--accent); color: #fff; border: none; border-radius: 6px; cursor: pointer;
    flex-shrink: 0;
  }
  .btn-small:hover { background: var(--accent-hover); }
  .btn-cancel-sm {
    padding: 0.45rem 0.7rem; font-size: 0.8rem;
    background: none; border: 1px solid var(--btn-cancel-border);
    border-radius: 6px; color: var(--text-secondary); cursor: pointer; flex-shrink: 0;
  }
  .btn-cancel-sm:hover { background: var(--bg-tertiary); color: var(--text-primary); }
  .browser-sort-bar {
    display: flex; align-items: center; gap: 0.4rem;
    margin-bottom: 0.4rem; font-size: 0.78rem;
  }
  .sort-label { color: var(--text-muted); }
  .sort-spacer { flex: 1; }
  .btn-sort {
    padding: 0.25rem 0.6rem; font-size: 0.78rem;
    background: var(--bg-tertiary); border: 1px solid var(--border-color);
    border-radius: 4px; color: var(--text-secondary); cursor: pointer;
  }
  .btn-sort:hover { border-color: var(--accent); color: var(--text-primary); }
  .btn-sort.active { background: var(--accent); color: #fff; border-color: var(--accent); }
  .browser-create-row {
    display: flex; align-items: center; gap: 0.4rem; margin-bottom: 0.4rem;
  }
  .browser-create-row input {
    flex: 1; padding: 0.4rem 0.6rem; font-size: 0.8rem;
    background: var(--bg-tertiary); border: 1px solid var(--border-color);
    border-radius: 6px; color: var(--text-primary); outline: none;
  }
  .browser-create-row input:focus { border-color: var(--accent); }
  .create-error { font-size: 0.75rem; color: var(--danger); }
  .browser-list {
    flex: 1; overflow-y: auto; border: 1px solid var(--border-color);
    border-radius: 8px; min-height: 200px; max-height: 40vh;
    margin-bottom: 0.75rem;
  }
  .browser-center { display: flex; align-items: center; justify-content: center; height: 200px; }
  .browser-error { padding: 1rem; color: var(--danger); font-size: 0.85rem; text-align: center; }
  .browser-empty { padding: 1rem; color: var(--text-muted); font-size: 0.85rem; text-align: center; }
  .browser-section {
    padding: 0.4rem 0.75rem; font-size: 0.7rem; font-weight: 700;
    text-transform: uppercase; letter-spacing: 0.04em;
    color: var(--text-muted); background: var(--bg-tertiary);
    border-bottom: 1px solid var(--border-color);
  }
  .browser-entry {
    display: flex; align-items: center; gap: 0.7rem;
    padding: 0.55rem 0.75rem; cursor: pointer; font-size: 0.85rem;
    color: var(--text-primary); border-bottom: 1px solid var(--border-color);
  }
  .browser-entry:last-child { border-bottom: none; }
  .browser-entry:hover { background: var(--bg-hover); }
  .browser-entry svg { flex-shrink: 0; color: var(--accent); }
  .side-entry .pin-icon { color: #f5a623; }
  .side-entry .mount-icon { color: var(--text-muted); }
  .entry-name { flex: 1; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
  .entry-path {
    font-size: 0.72rem; color: var(--text-muted); font-family: monospace;
    max-width: 45%; overflow: hidden; text-overflow: ellipsis; white-space: nowrap;
    flex-shrink: 0;
  }
  .entry-date { font-size: 0.75rem; color: var(--text-muted); flex-shrink: 0; }
  .entry-del { color: var(--text-muted); }
  .entry-del:hover { color: var(--danger); background: none; }
  .browser-footer {
    display: flex; gap: 0.5rem; justify-content: flex-end;
    padding-bottom: 96px;
  }
  .btn-cancel {
    padding: 0.55rem 1.1rem; background: none; border: 1px solid var(--btn-cancel-border);
    border-radius: 8px; color: var(--text-secondary); font-size: 0.85rem; cursor: pointer;
  }
  .btn-cancel:hover { background: var(--bg-tertiary); color: var(--text-primary); }
  .btn-primary {
    padding: 0.55rem 1.1rem; background: var(--accent); border: none; border-radius: 8px;
    color: #fff; font-size: 0.85rem; font-weight: 600; cursor: pointer;
  }
  .btn-primary:hover { background: var(--accent-hover); }

  @keyframes fadeIn { from { opacity: 0; } to { opacity: 1; } }
  @keyframes scaleIn { from { opacity: 0; transform: scale(0.95); } to { opacity: 1; transform: scale(1); } }

  @media (max-width: 768px) {
    .browser-modal {
      position: fixed; top: 0; left: 0; right: 0; bottom: 0;
      border-radius: 0; min-width: auto;
      max-width: 100vw; max-height: 100dvh;
      padding-bottom: 96px;
    }
    .browser-list { max-height: none; flex: 1; }
    .browser-footer { padding-bottom: 96px; }
  }
</style>
