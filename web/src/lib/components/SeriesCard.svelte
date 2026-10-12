<script>
  // Shell condivisa delle card serie (Gestione e Scopri): poster + titolo +
  // riga meta + azioni. La shell non conosce Gestione né Scopri: titolo e
  // immagini via prop, contenuti variabili via snippet. CSS dei contenuti
  // variabili nei componenti che li definiscono; qui solo layout della shell.
  // Accessibilità: apertura con button nativo dedicato + azioni fratelli
  // (mai card-intera role=button con controlli dentro, mai button annidati).
  let {
    title = '',
    poster = '',
    srcset = '',
    large = false,
    index = 0,
    openLabel = '',
    onopen,
    meta = null,
    actions = null,
  } = $props();
</script>

<div class="series-card" style="--i:{index}">
  <div class="card-poster-wrap">
    <button type="button" class="card-open" onclick={() => onopen?.()} aria-label={openLabel || title}>
      <img class="card-poster" src={poster} srcset={srcset}
        sizes={large ? '(max-width: 768px) 50vw, 240px' : '(max-width: 768px) 50vw, 180px'}
        width="480" height="720" alt="" loading="lazy" decoding="async" />
      <span class="card-title-overlay">
        <span class="card-title">{title}</span>
        <span class="card-meta-row">{@render meta?.()}</span>
      </span>
    </button>
    <div class="card-hover-actions">
      <button type="button" class="btn-hover action-info" title="Dettagli" aria-label="Dettagli"
        onclick={(e) => { e.stopPropagation(); onopen?.(); }}>
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="9"/><line x1="12" y1="11" x2="12" y2="16"/><line x1="12" y1="8" x2="12.01" y2="8"/></svg>
      </button>
      {@render actions?.()}
    </div>
  </div>
</div>

<style>
  .series-card {
    background: var(--bg-secondary); border: 1px solid var(--border-color); border-radius: 12px; overflow: hidden;
    display: flex; flex-direction: column; min-width: 0;
    transition: box-shadow var(--motion-enter) var(--motion-ease-out);
    animation: motion-rise var(--motion-enter) var(--motion-ease-out) backwards;
    animation-delay: min(calc(var(--i, 0) * var(--motion-stagger)), var(--motion-stagger-max));
  }
  @media (hover: hover) {
    .series-card:hover {
      transform: scale(1);
      box-shadow: 0 8px 30px rgba(0,0,0,0.35);
      z-index: 2;
    }
    .series-card:hover .card-hover-actions {
      opacity: 1; visibility: visible;
    }
  }
  .series-card:has(:focus-visible) .card-hover-actions {
    opacity: 1; visibility: visible;
  }
  .card-poster-wrap {
    position: relative;
    overflow: hidden;
  }
  /* Apertura: button blocco a tutta card (titolo incluso), senza stili da bottone. */
  .card-open {
    display: block; width: 100%; padding: 0; border: none; background: none;
    cursor: pointer; text-align: inherit; color: inherit; font: inherit;
  }
  .card-open:focus-visible { outline: 2px solid var(--accent); outline-offset: -2px; }
  .card-poster {
    width: 100%; height: auto; aspect-ratio: 2 / 3; object-fit: contain;
    background: var(--bg-tertiary);
    display: block;
    transform: translateZ(0);
  }
  .card-hover-actions {
    position: absolute; inset: 0;
    display: flex; align-items: center; justify-content: center; gap: 0.6rem;
    background: rgba(0,0,0,0.45);
    opacity: 0; visibility: hidden;
    transition: opacity var(--motion-fast) var(--motion-ease-out), visibility var(--motion-fast);
    pointer-events: none;
  }
  /* Solo i bottoni restano cliccabili sopra l'overlay: il resto passa al button di apertura. */
  .card-hover-actions :global(.btn-hover) { pointer-events: auto; }
  /* Contenuti degli snippet (definiti nei consumer): selettori globali sotto
     scope della shell — un solo proprietario, niente duplicazioni. */
  .series-card :global(.btn-hover) {
    width: 2.5rem; height: 2.5rem; border-radius: 50%;
    border: 1px solid var(--action-color, rgba(255,255,255,0.35));
    background: var(--action-bg, rgba(20,20,25,0.75)); color: var(--action-color, #fff); cursor: pointer;
    display: flex; align-items: center; justify-content: center;
  }
  .series-card :global(.btn-hover:hover), .series-card :global(.btn-hover:focus-visible) {
    background: var(--action-color, var(--accent)); border-color: var(--action-color, var(--accent));
    color: var(--action-ink, #fff);
  }
  .series-card :global(.btn-hover:disabled) { opacity: 0.45; cursor: not-allowed; }
  .series-card :global(.btn-hover:disabled:hover) { background: var(--action-bg, rgba(20,20,25,0.75)); color: var(--action-color, #fff); }
  .series-card :global(.btn-hover svg) { width: 1.1rem; height: 1.1rem; }
  .card-title-overlay {
    position: absolute; left: 0; right: 0; bottom: 0;
    padding: 1.6rem 0.6rem 0.55rem;
    background: linear-gradient(to top, rgba(0,0,0,0.88) 0%, rgba(0,0,0,0.45) 55%, transparent 100%);
    color: #fff;
    display: block;
  }
  .card-title {
    display: -webkit-box; -webkit-line-clamp: 2; -webkit-box-orient: vertical; overflow: hidden;
    font-size: 0.85rem; font-weight: 600; margin-bottom: 0.2rem; color: #fff;
  }
  .card-meta-row { display: flex; align-items: center; gap: 0.4rem; }
  /* Badge/testo degli snippet meta (consumer): stesso vocabolario overlay. */
  .series-card :global(.card-service) {
    display: inline-block; font-size: 0.65rem; color: #fff;
    background: rgba(255,255,255,0.18); padding: 0.1rem 0.4rem; border-radius: 4px;
  }
  .series-card :global(.card-epcount) { font-size: 0.65rem; color: rgba(255,255,255,0.8); }
  .series-card :global(.card-managed) {
    display: inline-block; font-size: 0.65rem; font-weight: 700; color: #fff;
    background: var(--success); padding: 0.1rem 0.4rem; border-radius: 4px;
  }

  @media (max-width: 768px) {
    .series-card:hover { transform: none; box-shadow: none; }
  }
</style>
