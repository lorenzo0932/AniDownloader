<script>
  // Shell condivisa delle righe serie (vista tabella di Gestione e Scopri):
  // miniatura + nome + riga meta + azioni. Contenuti variabili via snippet;
  // la shell non conosce i due contesti. Apertura con button nativo dedicato
  // + azioni fratelli (mai riga-intera role=button con controlli dentro).
  let {
    title = '',
    thumb = '',
    index = 0,
    openLabel = '',
    onopen,
    meta = null,
    badge = null,
    actions = null,
  } = $props();
</script>

<div class="series-row" style="--i:{index}">
  <button type="button" class="row-open" onclick={() => onopen?.()} aria-label={openLabel || title}>
    <img class="row-poster" src={thumb} alt="" loading="lazy" decoding="async" width="40" height="56" />
    <span class="row-info">
      <span class="row-name">{title}</span>
      <span class="row-meta">
        <span class="row-meta-text">{@render meta?.()}</span>
        {@render badge?.()}
      </span>
    </span>
  </button>
  <div class="row-actions">{@render actions?.()}</div>
</div>

<style>
  .series-row {
    display: flex; align-items: center; gap: 0.75rem;
    background: var(--bg-secondary); border: 1px solid var(--border-color);
    border-radius: 10px; padding: 0.4rem 0.6rem;
    transition: border-color var(--motion-fast) var(--motion-ease-out);
    animation: motion-rise var(--motion-enter) var(--motion-ease-out) backwards;
    animation-delay: min(calc(var(--i, 0) * var(--motion-stagger)), var(--motion-stagger-max));
  }
  @media (hover: hover) {
    .series-row:hover { border-color: var(--accent); }
  }
  /* Apertura: riga cliccabile senza stili da bottone. */
  .row-open {
    flex: 1; min-width: 0; display: flex; align-items: center; gap: 0.75rem;
    padding: 0; border: none; background: none; cursor: pointer;
    text-align: inherit; color: inherit; font: inherit;
  }
  .row-open:focus-visible { outline: 2px solid var(--accent); outline-offset: 2px; border-radius: 6px; }
  /* Contenuti degli snippet (definiti nei consumer): selettori globali sotto
     scope della shell — un solo proprietario, niente duplicazioni, nessuna
     collisione con altre liste (es. TaskItem). */
  .series-row :global(.row-poster) {
    width: 40px; height: 56px; border-radius: 4px; object-fit: contain;
    background: var(--bg-tertiary); flex-shrink: 0;
  }
  .series-row :global(.row-info) {
    flex: 1; min-width: 0;
    display: flex; flex-direction: column; gap: 0.15rem;
  }
  .series-row :global(.row-name) {
    font-size: 0.88rem; font-weight: 600; color: var(--text-primary);
    white-space: nowrap; overflow: hidden; text-overflow: ellipsis;
  }
  .series-row :global(.row-meta) {
    font-size: 0.72rem; color: var(--text-muted);
    display: flex; align-items: center; gap: 0.35rem;
    min-width: 0; overflow: hidden; white-space: nowrap;
  }
  .series-row :global(.row-meta-text) {
    display: flex; align-items: center;
    min-width: 0; overflow: hidden; white-space: nowrap;
  }
  .series-row :global(.row-meta-text > span) { flex-shrink: 0; }
  .series-row :global(.row-meta-text > span + span::before) {
    content: '·'; margin: 0 0.35rem; color: var(--text-muted-more, var(--text-muted));
  }
  .series-row :global(.row-service) { color: var(--accent); }
  .series-row :global(.row-date) { color: var(--text-muted); }
  .series-row :global(.row-meta-text > .row-date) {
    flex-shrink: 1; overflow: hidden; text-overflow: ellipsis; min-width: 0;
  }
  .series-row :global(.row-badge) {
    font-size: 0.66rem; font-weight: 600; padding: 0.08rem 0.4rem;
    border-radius: 20px; background: var(--bg-tertiary); color: var(--text-muted);
    white-space: nowrap; flex-shrink: 0;
  }
  .series-row :global(.row-badge-warn) { color: var(--warning-text); background: var(--warning-bg); }
  .series-row :global(.row-badge-managed) { color: var(--success); background: var(--success-border); }
  @media (max-width: 560px) {
    .series-row :global(.row-meta-text > .row-date) { display: none; }
  }
  .row-actions { display: flex; gap: 0.25rem; flex-shrink: 0; }
  .series-row :global(.btn-row) {
    background: none; border: none; color: var(--text-muted);
    padding: 0.45rem; cursor: pointer; border-radius: 6px;
    display: flex; align-items: center; justify-content: center;
  }
  .series-row :global(.btn-row:hover) { color: var(--accent); background: var(--bg-tertiary); }
  .series-row :global(.btn-row.danger:hover) { color: var(--danger); }
  .series-row :global(.btn-row:disabled) { opacity: 0.45; cursor: not-allowed; }
  .series-row :global(.btn-row:disabled:hover) { color: var(--text-muted); background: none; }

</style>
