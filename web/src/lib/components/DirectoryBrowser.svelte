<script>
  import { BASE } from '../api.js';

  let { show = false, currentPath = '~/Video', title = 'Sfoglia Directory', onselect, oncancel } = $props();

  let path = $state('');
  let entries = $state([]);
  let loading = $state(false);
  let error = $state('');
  let manualInput = $state('');
  let sortMode = $state('name');

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

  let sorted = $derived.by(() => {
    if (sortMode === 'date') {
      return [...entries].sort((a, b) => ((b.mtime || 0) - (a.mtime || 0)));
    }
    return entries;
  });

  async function load(pathToLoad) {
    loading = true;
    error = '';
    entries = [];
    try {
      const res = await fetch(BASE + '/api/browse?path=' + encodeURIComponent(pathToLoad));
      const data = await res.json();
      if (data.success && data.entries) {
        entries = data.entries;
        path = pathToLoad;
      } else {
        error = data.error || 'Errore nel caricamento';
      }
    } catch (e) {
      error = 'Errore di rete: ' + e.message;
    } finally {
      loading = false;
    }
  }

  function goUp() {
    const idx = path.lastIndexOf('/');
    if (idx <= 0) { load('/'); return; }
    const parent = path.substring(0, idx);
    load(parent || '/');
  }

  function enterDir(dirPath) {
    load(dirPath);
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

  $effect(() => {
    if (show && currentPath) {
      path = currentPath;
      load(currentPath);
      manualInput = currentPath;
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
        <span class="browser-path">{path}</span>
        <button type="button" class="btn-icon-sm" onclick={goUp} title="Cartella superiore">
          <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" width="20" height="20"><path d="M5 12h14M12 5l-7 7 7 7"/></svg>
        </button>
      </div>

      <div class="browser-manual-row">
        <input type="text" bind:value={manualInput} placeholder="Inserisci percorso manualmente..." />
        <button type="button" class="btn-small" onclick={handleManualBrowse}>Vai</button>
      </div>

      <div class="browser-sort-bar">
        <span class="sort-label">Ordina:</span>
        <button type="button" class="btn-sort" class:active={sortMode === 'name'} onclick={() => sortMode = 'name'}>Nome</button>
        <button type="button" class="btn-sort" class:active={sortMode === 'date'} onclick={() => sortMode = 'date'}>Data</button>
      </div>

      <div class="browser-list">
        {#if loading}
          <div class="browser-center"><div class="spinner-sm"></div></div>
        {:else if error}
          <div class="browser-error">{error}</div>
        {:else if sorted.length === 0}
          <div class="browser-empty">Nessuna sottodirectory</div>
        {:else}
          <div class="browser-entry" onclick={() => goUp()} role="button" tabindex="0" onkeydown={(e) => e.key === 'Enter' && goUp()}>
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" width="22" height="22"><path d="M5 12h14M12 5l-7 7 7 7"/></svg>
            <span class="entry-name">.. (su)</span>
          </div>
          {#each sorted as entry}
            <div class="browser-entry" onclick={() => enterDir(entry.path)} role="button" tabindex="0" onkeydown={(e) => e.key === 'Enter' && enterDir(entry.path)}>
              <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" width="22" height="22"><path d="M22 19a2 2 0 01-2 2H4a2 2 0 01-2-2V5a2 2 0 012-2h5l2 3h9a2 2 0 012 2z"/></svg>
              <span class="entry-name">{entry.name}</span>
              {#if sortMode === 'date' && entry.mtime}
                <span class="entry-date">{formatMtime(entry.mtime)}</span>
              {/if}
            </div>
          {/each}
        {/if}
      </div>

      <div class="browser-footer">
        <button type="button" class="btn-cancel" onclick={oncancel}>Annulla</button>
        <button type="button" class="btn-primary" onclick={handleSelect}>Seleziona questa cartella</button>
      </div>
    </div>
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
    padding: 0.25rem; display: flex; border-radius: 4px;
  }
  .btn-icon-sm:hover { background: var(--bg-hover); color: var(--text-primary); }
  .browser-manual-row {
    display: flex; gap: 0.4rem; margin-bottom: 0.5rem;
  }
  .browser-manual-row input {
    flex: 1; padding: 0.45rem 0.7rem; font-size: 0.8rem; font-family: monospace;
    background: var(--bg-tertiary); border: 1px solid var(--border-color);
    border-radius: 6px; color: var(--text-primary); outline: none;
  }
  .browser-manual-row input:focus { border-color: var(--accent); }
  .btn-small {
    padding: 0.45rem 0.85rem; font-size: 0.8rem;
    background: var(--accent); color: #fff; border: none; border-radius: 6px; cursor: pointer;
  }
  .btn-small:hover { background: var(--accent-hover); }
  .browser-sort-bar {
    display: flex; align-items: center; gap: 0.4rem;
    margin-bottom: 0.4rem; font-size: 0.78rem;
  }
  .sort-label { color: var(--text-muted); }
  .btn-sort {
    padding: 0.25rem 0.6rem; font-size: 0.78rem;
    background: var(--bg-tertiary); border: 1px solid var(--border-color);
    border-radius: 4px; color: var(--text-secondary); cursor: pointer;
  }
  .btn-sort:hover { border-color: var(--accent); color: var(--text-primary); }
  .btn-sort.active { background: var(--accent); color: #fff; border-color: var(--accent); }
  .browser-list {
    flex: 1; overflow-y: auto; border: 1px solid var(--border-color);
    border-radius: 8px; min-height: 200px; max-height: 40vh;
    margin-bottom: 0.75rem;
  }
  .browser-center { display: flex; align-items: center; justify-content: center; height: 200px; }
  .browser-error { padding: 1rem; color: var(--danger); font-size: 0.85rem; text-align: center; }
  .browser-empty { padding: 1rem; color: var(--text-muted); font-size: 0.85rem; text-align: center; }
  .browser-entry {
    display: flex; align-items: center; gap: 0.7rem;
    padding: 0.55rem 0.75rem; cursor: pointer; font-size: 0.85rem;
    color: var(--text-primary); border-bottom: 1px solid var(--border-color);
  }
  .browser-entry:last-child { border-bottom: none; }
  .browser-entry:hover { background: var(--bg-hover); }
  .browser-entry svg { flex-shrink: 0; color: var(--accent); }
  .entry-name { flex: 1; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
  .entry-date { font-size: 0.75rem; color: var(--text-muted); flex-shrink: 0; }
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
