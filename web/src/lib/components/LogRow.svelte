<script>
  // Riga di log strutturata: timestamp abbreviato, badge livello e messaggio
  // monospace con evidenziazione della ricerca. Il testo completo resta in
  // `title` (raw) per il tooltip.
  let { entry, query = '', wrap = false } = $props();

  const LIVELLI = { ERROR: 'error', WARN: 'warn', INFO: 'info', RESULT: 'result' };

  let livello = $derived(LIVELLI[entry.level] || 'altro');
  let ora = $derived(entry.time ? entry.time.slice(11, 19) : '');
  let segmenti = $derived(evidenzia(entry.message, query));

  function evidenzia(testo, q) {
    if (!testo) return [];
    const needle = (q || '').trim();
    if (!needle) return [{ text: testo, mark: false }];
    const lower = testo.toLowerCase();
    const needleLower = needle.toLowerCase();
    const parti = [];
    let da = 0;
    while (da < testo.length) {
      const i = lower.indexOf(needleLower, da);
      if (i === -1) {
        parti.push({ text: testo.slice(da), mark: false });
        break;
      }
      if (i > da) parti.push({ text: testo.slice(da, i), mark: false });
      parti.push({ text: testo.slice(i, i + needleLower.length), mark: true });
      da = i + needleLower.length;
    }
    return parti;
  }
</script>

<div class="log-row" class:wrap data-level={entry.level} title={entry.raw}>
  <span class="log-time" title={entry.time || undefined}>{ora || '—'}</span>
  <span class="log-level livello-{livello}">{entry.level}</span>
  <span class="log-message">{#each segmenti as parte, i (i)}{#if parte.mark}<mark>{parte.text}</mark>{:else}{parte.text}{/if}{/each}</span>
</div>

<style>
  .log-row {
    display: grid;
    grid-template-columns: 64px 64px minmax(0, 1fr);
    gap: 0.5rem;
    align-items: start;
    padding: 2px 0.75rem;
    min-height: 22px;
    line-height: 1.5;
    font-family: 'Consolas', 'Menlo', 'Monaco', 'DejaVu Sans Mono', 'Noto Sans Mono', 'Courier New', monospace;
    font-size: 0.76rem;
    content-visibility: auto;
    contain-intrinsic-size: 22px;
  }
  .log-row:hover { background: var(--bg-tertiary); }

  .log-time {
    color: var(--text-muted);
    font-variant-numeric: tabular-nums;
    white-space: nowrap;
    padding-top: 1px;
  }

  .log-level {
    justify-self: start;
    padding: 0 4px;
    border-radius: 4px;
    font-size: 0.62rem;
    font-weight: 700;
    letter-spacing: 0.04em;
    white-space: nowrap;
    margin-top: 2px;
  }
  .livello-error { color: var(--danger); background: var(--danger-bg); }
  .livello-warn { color: var(--warning-text); background: var(--warning-bg); }
  .livello-info { color: var(--text-secondary); background: var(--bg-tertiary); }
  .livello-result { color: var(--accent-light); background: var(--accent-bg); }
  .livello-altro { color: var(--text-muted); background: transparent; }

  .log-message {
    color: var(--text-secondary);
    white-space: pre;
    min-width: 0;
    padding-top: 1px;
  }
  .log-row.wrap .log-message {
    white-space: pre-wrap;
    overflow-wrap: anywhere;
  }

  mark {
    background: var(--accent-bg);
    color: var(--accent-light);
    border-radius: 2px;
    padding: 0 1px;
  }
</style>
