<script>
  // Toolbar di avvio/stop download + stato complessivo + barra di progresso
  // globale. Stato e azioni arrivano come props dal parent (StatusPage).
  let { downloadRunning = false, overallStatus = '', sseConnected = false, phase = 'idle', summary = null,
        globalPercent = 0, activeCount = 0, doneCount = 0, skippedCount = 0, onstart, onstop } = $props();
</script>

<div class="toolbar">
  <button class="btn-primary" onclick={onstart} disabled={downloadRunning}>
    Avvia Download
  </button>
  <button class="btn-danger" onclick={onstop} disabled={!downloadRunning}>
    Ferma Download
  </button>
  {#if phase === 'analysis' && !summary}
    <span class="loading-spinner"></span>
  {/if}
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

<style>
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
  .status-label { font-size: 0.9rem; color: var(--text-secondary); font-weight: 600; flex: 1; }
  .sse-badge {
    font-size: 0.75rem; color: var(--text-muted); background: var(--bg-secondary); border: 1px solid var(--border-color);
    padding: 0.2rem 0.5rem; border-radius: 4px;
  }
  .sse-badge.connected { color: var(--success); border-color: var(--success-border); }

  .loading-spinner {
    width: 16px; height: 16px; border: 2px solid var(--border-color);
    border-top: 2px solid var(--accent); border-radius: 50%;
    animation: spin 0.8s linear infinite;
    flex-shrink: 0;
  }
  @keyframes spin { to { transform: rotate(360deg); } }

  .global-bar-wrap { display: flex; align-items: center; gap: 0.75rem; }
  .global-bar {
    flex: 1; height: 10px; background: var(--border-color); border-radius: 5px; overflow: hidden;
  }
  .global-bar-fill {
    height: 100%; background: linear-gradient(90deg, var(--accent), var(--accent-light));
    border-radius: 5px; transition: width 0.3s ease;
  }
  .global-bar-text { font-size: 0.8rem; color: var(--text-muted); white-space: nowrap; }

  @media (max-width: 768px) {
    .toolbar { flex-direction: column; align-items: stretch; }
    .toolbar .btn-primary, .toolbar .btn-danger { width: 100%; justify-content: center; }
    .status-label { text-align: center; }
    .sse-badge { align-self: flex-end; }
    .global-bar-wrap { flex-direction: column; align-items: stretch; gap: 0.25rem; }
    .global-bar-text { white-space: normal; text-align: center; }
  }
</style>
