<script>
  // Toolbar avvio/stop, stato live e stato globale (coda/problemi).
  let { downloadRunning = false, starting = false, overallStatus = '', sseConnected = false,
        phase = 'idle', summary = null, codaTotale = 0, problemiAperti = 0,
        oncoda, onstart, onstop } = $props();
</script>

<div class="toolbar" class:running={downloadRunning}>
  {#if downloadRunning}
    <button class="btn-danger" onclick={onstop}>Ferma Download</button>
  {:else}
    <button class="btn-primary" onclick={onstart} disabled={starting}>
      {starting ? 'Avvio…' : 'Avvia Download'}
    </button>
  {/if}
  {#if phase === 'analysis' && !summary}
    <span class="loading-spinner"></span>
  {/if}
  {#if overallStatus}
    <span class="status-label" role="status">{overallStatus}</span>
  {/if}
  {#if codaTotale > 0 || problemiAperti > 0}
    <span class="header-chips">
      {#if codaTotale > 0}
        <a class="status-chip" href="/attivita" onclick={oncoda}>{codaTotale} in coda</a>
      {/if}
      {#if problemiAperti > 0}
        <a class="status-chip status-chip-problem" href="/attivita?tab=problemi">
          {problemiAperti} {problemiAperti === 1 ? 'problema' : 'problemi'}
        </a>
      {/if}
    </span>
  {/if}
  {#if !sseConnected}
    <span class="sse-badge">Riconnessione…</span>
  {/if}
</div>


<style>
  .toolbar { display: flex; align-items: center; gap: 0.75rem; flex-wrap: wrap; }
  .btn-primary {
    min-height: 36px; padding: 0.4rem 0.85rem; background: var(--accent); border: none; border-radius: 8px;
    color: #fff; font-size: 0.85rem; font-weight: 600; cursor: pointer;
  }
  .btn-primary:hover { background: var(--accent-hover); }
  .btn-primary:disabled { opacity: 0.5; cursor: not-allowed; }
  .btn-danger {
    min-height: 36px; padding: 0.4rem 0.85rem; background: transparent; border: 1px solid var(--danger-border); border-radius: 8px;
    color: var(--danger); font-size: 0.85rem; font-weight: 600; cursor: pointer;
  }
  .btn-danger:hover { background: var(--danger-bg-hover); }
  .btn-danger:disabled { opacity: 0.5; cursor: not-allowed; }
  .status-label { font-size: 0.85rem; color: var(--text-secondary); font-weight: 500; flex: 1; }

  .header-chips {
    margin-left: auto; display: flex; flex-wrap: wrap;
    align-items: center; gap: 0.45rem;
  }
  .status-chip {
    display: inline-flex; align-items: center; min-height: 26px;
    padding: 0.1rem 0.55rem; border: 1px solid var(--border-color);
    border-radius: 999px; background: var(--bg-secondary);
    color: var(--text-secondary); font-size: 0.78rem; font-weight: 500;
    text-decoration: none;
  }
  .status-chip:hover { background: var(--bg-hover); color: var(--text-primary); }
  .status-chip:focus-visible { outline: 2px solid var(--accent); outline-offset: 2px; }
  .status-chip-problem {
    color: var(--danger); border-color: var(--danger-border);
    background: var(--danger-bg);
  }
  .status-chip-problem:hover { background: var(--danger-bg-hover); color: var(--danger); }

  .sse-badge {
    margin-left: auto;
    font-size: 0.75rem; color: var(--text-muted); background: var(--bg-secondary); border: 1px solid var(--border-color);
    padding: 0.2rem 0.5rem; border-radius: 4px;
  }

  .loading-spinner {
    width: 16px; height: 16px; border: 2px solid var(--border-color);
    border-top: 2px solid var(--accent); border-radius: 50%;
    animation: motion-spin var(--motion-spinner) linear infinite;
    flex-shrink: 0;
  }

  @media (max-width: 768px) {
    .toolbar { display: grid; grid-template-columns: minmax(0, 1fr) auto; gap: 0.5rem; align-items: center; }
    .toolbar .btn-primary, .toolbar .btn-danger { grid-column: 2; grid-row: 1; padding: 0.4rem 0.65rem; }
    .status-label { grid-column: 1; grid-row: 1; min-width: 0; overflow-wrap: anywhere; font-size: 0.85rem; }
    .header-chips { grid-column: 1 / -1; grid-row: 2; justify-self: start; margin-left: 0; }
    .sse-badge { grid-column: 1 / -1; grid-row: 3; justify-self: start; }
    .loading-spinner { display: none; }
  }
  @media (any-pointer: coarse) {
    .toolbar .btn-primary, .toolbar .btn-danger { min-height: 44px; }
  }
</style>
