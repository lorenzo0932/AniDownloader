<script>
  // Card compatta per i controlli automatici che non hanno trovato episodi.
  // Un check ogni 15 minuti produce altrimenti decine di righe identiche:
  // qui diventano una sola voce con conteggio, orario e timeline.
  let { runs = [], expanded = false, onToggle } = $props();

  let count = $derived(runs.length);
  let detailId = $derived(
    runs[0]?.id ? `check-group-detail-${runs[0].id}` : 'check-group-detail'
  );
  let ultimo = $derived(runs[0] || null);
  let primo = $derived(runs[count - 1] || null);
  let serieControllate = $derived(ultimo?.saltati ?? 0);

  function ora(iso) {
    if (!iso) return '';
    const d = new Date(iso);
    if (Number.isNaN(d.getTime())) return '';
    return d.toLocaleTimeString('it-IT', { hour: '2-digit', minute: '2-digit' });
  }

  function tempoRelativo(iso) {
    if (!iso) return '';
    const t = Date.parse(iso);
    if (Number.isNaN(t)) return '';
    const sec = Math.max(0, Math.floor((Date.now() - t) / 1000));
    if (sec < 60) return 'adesso';
    if (sec < 3600) return `${Math.floor(sec / 60)} min fa`;
    if (sec < 86400) return `${Math.floor(sec / 3600)} h fa`;
    return `${Math.floor(sec / 86400)} g fa`;
  }

  function durata(run) {
    if (!run?.concluso_il || !run?.avviato_il) return '';
    const fine = Date.parse(run.concluso_il);
    const inizio = Date.parse(run.avviato_il);
    if (!Number.isFinite(fine) || !Number.isFinite(inizio) || fine < inizio) return '';
    const sec = Math.round((fine - inizio) / 1000);
    if (sec < 60) return `${sec}s`;
    return `${Math.floor(sec / 60)}m ${sec % 60}s`;
  }
</script>

<section class="check-group" data-check-group aria-label="Controlli automatici senza novità">
  <button class="group-main" onclick={() => onToggle?.()}
          aria-expanded={expanded ? 'true' : 'false'}
          aria-controls={detailId}>
    <span class="group-icon" aria-hidden="true">
      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
        <path d="M3 12a9 9 0 1 0 3-6.7"/>
        <path d="M3 4v5h5"/>
      </svg>
    </span>
    <span class="group-copy">
      <span class="group-top">
        <span class="group-title">Controlli automatici</span>
        <span class="chevron" aria-hidden="true" class:aperto={expanded}>▾</span>
      </span>
      <span class="group-meta">
        <span>{count} {count === 1 ? 'esecuzione' : 'esecuzioni'} senza nuovi episodi</span>
        {#if primo && ultimo}
          <span class="sep" aria-hidden="true">·</span>
          <span>{ora(primo.avviato_il)}–{ora(ultimo.avviato_il)}</span>
        {/if}
        {#if ultimo}
          <span class="sep" aria-hidden="true">·</span>
          <span>ultima {tempoRelativo(ultimo.avviato_il)}</span>
        {/if}
      </span>
    </span>
    <span class="group-count" aria-hidden="true">{count}</span>
  </button>

  {#if expanded}
    <div class="group-detail" id={detailId}>
      {#if serieControllate > 0}
        <p class="group-note">{serieControllate} serie controllate a ogni esecuzione.</p>
      {/if}
      <ul class="check-list">
        {#each runs as run (run.id)}
          <li class="check-row">
            <span class="check-time">{ora(run.avviato_il)}</span>
            <span class="check-esito">Nessun nuovo episodio</span>
            {#if durata(run)}
              <span class="check-durata">{durata(run)}</span>
            {/if}
          </li>
        {/each}
      </ul>
    </div>
  {/if}
</section>

<style>
  .check-group {
    background: var(--bg-primary);
    border: 1px solid var(--border-color);
    border-radius: 10px;
    overflow: hidden;
    animation: motion-rise var(--motion-enter) var(--motion-ease-out) backwards;
  }
  .group-main {
    width: 100%; display: flex; align-items: center; gap: 0.65rem;
    background: none; border: none; color: var(--text-primary);
    padding: 0.7rem 0.85rem; text-align: left; cursor: pointer; font-family: inherit;
  }
  .group-main:hover { background: var(--bg-hover); }
  .group-main:focus-visible { outline: 2px solid var(--accent); outline-offset: -2px; }
  .group-icon {
    width: 28px; height: 28px; flex: 0 0 auto; border-radius: 50%;
    display: grid; place-items: center;
    background: var(--bg-secondary); color: var(--text-muted);
  }
  .group-icon svg { width: 14px; height: 14px; }
  .group-copy { min-width: 0; flex: 1; display: flex; flex-direction: column; gap: 0.18rem; }
  .group-top { display: flex; align-items: center; gap: 0.5rem; min-width: 0; }
  .group-title {
    font-weight: 500; font-size: 0.88rem; color: var(--text-secondary);
    white-space: nowrap; overflow: hidden; text-overflow: ellipsis;
  }
  .chevron { color: var(--text-muted); font-size: 12px; margin-left: auto; }
  .chevron.aperto { transform: rotate(180deg); }
  .group-count {
    min-width: 22px; height: 20px; padding: 0 0.4rem; border-radius: 999px;
    display: inline-flex; align-items: center; justify-content: center;
    background: var(--bg-secondary); border: 1px solid var(--border-color);
    color: var(--text-muted); font-size: 11px; font-variant-numeric: tabular-nums;
  }
  .group-meta {
    display: flex; flex-wrap: wrap; align-items: center; gap: 0.2rem 0.4rem;
    color: var(--text-muted); font-size: 11px;
  }
  .sep { color: var(--text-muted-more); }

  .group-detail { border-top: 1px solid var(--border-color); padding: 0.35rem 0.85rem 0.5rem; }
  .group-note { color: var(--text-muted); font-size: 11px; margin: 0.2rem 0 0.35rem; }
  .check-list { list-style: none; margin: 0; padding: 0; }
  .check-row {
    display: flex; align-items: center; gap: 0.6rem;
    padding: 0.3rem 0; font-size: 12px; color: var(--text-muted);
    border-bottom: 1px solid var(--border-color);
  }
  .check-row:last-child { border-bottom: none; }
  .check-time { font-variant-numeric: tabular-nums; color: var(--text-secondary); min-width: 44px; }
  .check-esito { flex: 1; }
  .check-durata { font-variant-numeric: tabular-nums; color: var(--text-muted-more); }
</style>
