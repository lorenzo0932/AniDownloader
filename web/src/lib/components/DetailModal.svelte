<script module>
  let nextDetailId = 0;
</script>

<script>
  // Shell condivisa del modale dettaglio (Gestione, Home sola lettura,
  // Scopri): overlay, pannello, locandina + lightbox, topbar, chiusura.
  // Titolo e immagini via prop; meta/corpo/dettagli/azioni via snippet —
  // la shell non conosce i tre contesti. Accessibilità: dialog con titolo
  // associato, focus iniziale nel pannello, contenimento Tab, ritorno al
  // chiamante, Escape stratificato (lightbox prima, modale poi).
  import { fade, fly, scale } from 'svelte/transition';

  let {
    title = '',
    poster = '',
    srcset = '',
    fullPoster = '',
    backLabel = 'Serie',
    onclose,
    meta = null,
    body = null,
    details = null,
    actions = null,
  } = $props();
  let showLightbox = $state(false);
  let panel = $state(null);
  let opener = null;

  // Id titolo unico per aria-labelledby (un modale alla volta in pratica,
  // ma niente id duplicati se mai se ne aprissero due).
  const titleId = `detail-title-${++nextDetailId}`;

  $effect(() => {
    opener = document.activeElement;
    panel?.focus();
    return () => {
      if (opener && document.contains(opener)) opener.focus();
    };
  });

  function onOverlayKey(e) {
    if (e.key === 'Escape') {
      if (showLightbox) showLightbox = false;
      else onclose?.();
      return;
    }
    // Contenimento Tab nel pannello (la lightbox ha un solo controllo).
    if (e.key !== 'Tab' || !panel) return;
    const focusables = panel.querySelectorAll(
      'button:not(:disabled), a[href], [tabindex]:not([tabindex="-1"])');
    if (focusables.length === 0) return;
    const first = focusables[0];
    const last = focusables[focusables.length - 1];
    if (e.shiftKey && document.activeElement === first) {
      e.preventDefault();
      last.focus();
    } else if (!e.shiftKey && document.activeElement === last) {
      e.preventDefault();
      first.focus();
    }
  }
</script>

<!-- Ingresso via CSS (sempre visibile) + uscita via out: (simmetrica).
     transition: copriva entrambe ma l'ingresso non risultava percepibile. -->
<div class="modal-overlay" out:fade={{ duration: 150 }} onclick={onclose} onkeydown={onOverlayKey} role="dialog" aria-modal="true" aria-labelledby={titleId} tabindex="-1">
  <!-- Pannello con rise (fly y+fade): lo scale 0.95 era impercettibile,
       specie sul modale fullscreen mobile dove solo il testo sembrava
       animarsi. Stesso vocabolario delle pagine (fly y:8). -->
  <div class="modal-panel detail-modal" out:fly={{ y: 16, duration: 200 }} onclick={(e) => e.stopPropagation()} role="presentation" bind:this={panel} tabindex="-1">
    <div class="detail-modal-inner">
      <div class="detail-poster-col">
        <button type="button" class="detail-poster-btn" onclick={() => showLightbox = true} title="Ingrandisci locandina" aria-label="Ingrandisci locandina">
          <img class="detail-poster-ambient" src={poster} alt="" aria-hidden="true" />
          <img class="detail-poster" src={poster} srcset={srcset}
            sizes="(max-width: 768px) 160px, 260px"
            width="480" height="720" alt="" loading="lazy" decoding="async" />
        </button>
      </div>
      <div class="detail-info-col">
        <div class="detail-topbar">
          <button type="button" class="modal-back" onclick={onclose} title="Torna alle serie" aria-label="Torna alle serie">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" width="14" height="14"><path d="M19 12H5M12 19l-7-7 7-7"/></svg>
            <span>{backLabel}</span>
          </button>
          <button class="detail-close" onclick={onclose} aria-label="Chiudi">&times;</button>
        </div>
        <div class="detail-info-scroll">
          <h2 class="detail-title" id={titleId}>{title}</h2>
          <div class="detail-meta">{@render meta?.()}</div>
          <div class="detail-body">{@render body?.()}</div>
          {@render details?.()}
        </div>
        {@render actions?.()}
      </div>
    </div>
    {#if showLightbox}
      <div class="lightbox" out:fade={{ duration: 150 }} onclick={() => showLightbox = false} role="dialog" aria-modal="true" aria-label="Locandina a tutto schermo" title="Chiudi">
        <img class="lightbox-img" out:scale={{ duration: 200, start: 0.95 }} src={fullPoster || poster} alt="" />
      </div>
    {/if}
  </div>
</div>

<style>
  .modal-overlay {
    position: fixed; top: 0; left: 0; right: 0; bottom: 0;
    display: flex; align-items: center; justify-content: center;
    background: var(--overlay); z-index: 200;
    border: none; padding: 0; cursor: default;
    animation: fadeIn 0.2s ease;
  }
  .detail-modal {
    animation: panelIn 0.25s ease-out;
    max-width: 90vw; max-height: 90vh;
  }
  .detail-modal:focus { outline: none; }
  .detail-modal-inner {
    display: flex; gap: 0;
    background: var(--bg-secondary); border: 1px solid var(--border-color);
    border-radius: 16px; overflow: hidden;
    max-height: 80vh;
  }
  .detail-poster-col {
    flex-shrink: 0;
  }
  .detail-poster-btn {
    background: none; border: none; padding: 0; display: block; width: 100%; cursor: zoom-in;
  }
  /* Desktop: nascosto (la colonna 260px calza a pennello, niente bande). */
  .detail-poster-ambient { display: none; }
  .detail-poster {
    width: 260px; height: auto; aspect-ratio: 2/3; object-fit: contain;
    display: block; background: var(--bg-tertiary);
    transform: translateZ(0);
    transition: transform 0.2s ease, filter 0.2s ease;
  }
  @media (hover: hover) {
    .detail-poster-btn:hover .detail-poster { transform: scale(1.02); filter: brightness(1.06); }
  }
  /* Lightbox: locandina vincolata al viewport (mai oltre lo schermo,
     mai ingrandita oltre il naturale). Stesso vocabolario animazioni
     dei modali: fadeIn overlay + scaleIn contenuto. */
  .lightbox {
    position: fixed; inset: 0; z-index: 300;
    display: flex; align-items: center; justify-content: center;
    background: rgba(0, 0, 0, 0.9); cursor: zoom-out;
    animation: fadeIn 0.2s ease;
  }
  .lightbox-img {
    max-width: 100vw; max-height: 100dvh; object-fit: contain;
    animation: scaleIn 0.25s ease-out;
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
    background: none; border: none; color: var(--text-muted);
    font-size: 1.5rem; cursor: pointer; line-height: 1;
    padding: 0.25rem;
    transition: color 0.15s ease;
  }
  .detail-close:hover { color: var(--text-primary); }
  .detail-topbar {
    display: flex; align-items: center; justify-content: flex-end;
    margin-bottom: 0.5rem;
  }
  .modal-back {
    display: inline-flex; align-items: center; gap: 0.35rem;
    background: none; border: 1px solid var(--border-color); border-radius: 8px;
    color: var(--text-secondary); font-size: 0.8rem; font-weight: 600;
    padding: 0.35rem 0.6rem; cursor: pointer;
    transition: background 0.15s ease, color 0.15s ease, border-color 0.15s ease;
  }
  .modal-back:hover { background: var(--bg-tertiary); color: var(--text-primary); border-color: var(--accent); }
  /* Desktop: solo la X come prima; il back vive su mobile. */
  @media (min-width: 769px) {
    .detail-topbar .modal-back { display: none; }
  }
  .detail-title { font-size: 1.15rem; font-weight: 700; color: var(--text-primary); }
  .detail-meta { display: flex; flex-wrap: wrap; gap: 0.4rem; align-items: center; }
  .detail-body { display: contents; }
  /* Contenuti degli snippet (consumer): un solo proprietario qui. */
  .detail-modal :global(.card-service) {
    display: inline-block; font-size: 0.68rem; color: var(--accent);
    background: var(--card-service-bg); padding: 0.12rem 0.45rem; border-radius: 4px;
  }
  .detail-modal :global(.detail-badge) {
    font-size: 0.72rem; color: var(--success); background: var(--success-border);
    padding: 0.12rem 0.5rem; border-radius: 4px; font-weight: 600;
  }
  .detail-modal :global(.detail-badge-warn) { color: var(--warning-text); background: var(--warning-bg); }
  .detail-modal :global(.detail-badge-managed) { color: #fff; background: var(--success); }
  .detail-modal :global(.detail-desc) {
    font-size: 0.85rem; color: var(--text-secondary); line-height: 1.6;
    max-height: 200px; overflow-y: auto;
    animation: slideUp 0.3s ease-out;
  }
  .detail-modal :global(.detail-desc-empty) { color: var(--text-muted); font-style: italic; }
  .detail-modal :global(.detail-stats) { display: flex; flex-direction: column; gap: 0.5rem; }
  .detail-modal :global(.detail-stat) { display: flex; justify-content: space-between; align-items: center; }
  .detail-modal :global(.detail-stat-label) { font-size: 0.75rem; color: var(--text-muted); }
  .detail-modal :global(.detail-stat-value) { font-size: 0.85rem; color: var(--text-primary); font-weight: 600; }
  .detail-modal :global(.detail-stat-path) { font-size: 0.78rem; word-break: break-all; text-align: right; max-width: 55%; }
  .detail-modal :global(.detail-stat-url) { font-size: 0.75rem; font-weight: 400; word-break: break-all; text-align: right; max-width: 55%; }
  .detail-modal :global(.detail-stat-url:hover) { color: var(--accent); }
  .detail-modal :global(.detail-stat-diff) { color: var(--text-muted); font-size: 0.75rem; margin-left: 0.4rem; }
  .detail-modal :global(.detail-actions) { display: flex; gap: 0.5rem; padding-top: 0.75rem; border-top: 1px solid var(--border-color); flex-shrink: 0;
  }
  .detail-modal :global(.btn-primary) {
    padding: 0.55rem 1.1rem; background: var(--accent); border: none; border-radius: 8px;
    color: #fff; font-size: 0.85rem; font-weight: 600; cursor: pointer;
    display: flex; align-items: center; gap: 0.4rem;
    transition: background 0.15s ease;
  }
  .detail-modal :global(.btn-primary:hover) { background: var(--accent-hover); }
  .detail-modal :global(.btn-primary:disabled) { opacity: 0.55; cursor: not-allowed; }
  .detail-modal :global(.btn-danger) {
    padding: 0.55rem 1.1rem; background: var(--danger); border: none; border-radius: 8px;
    color: #fff; font-size: 0.85rem; font-weight: 600; cursor: pointer;
    display: flex; align-items: center; gap: 0.4rem;
    transition: opacity 0.15s ease;
  }
  .detail-modal :global(.btn-danger:hover) { opacity: 0.85; }
  .detail-modal :global(.btn-ghost) {
    padding: 0.55rem 1rem; border-radius: 8px; border: 1px solid var(--border-color);
    background: transparent; color: var(--text-secondary); cursor: pointer;
  }
  .detail-modal :global(.btn-ghost:hover) { background: var(--bg-tertiary); color: var(--text-primary); }
  .detail-modal :global(.detail-meta-list) { display: flex; flex-direction: column; gap: 0.2rem; font-size: 0.85rem; }
  .detail-modal :global(.detail-meta-list > div) { display: flex; gap: 0.5rem; }
  .detail-modal :global(.detail-meta-list dt) { color: var(--text-muted); min-width: 80px; }
  .detail-modal :global(.detail-meta-list dd) { margin: 0; color: var(--text-primary); }
  .detail-modal :global(.detail-loading) { color: var(--text-muted); }
  .detail-modal :global(.detail-error) { color: var(--danger); font-size: 0.85rem; }

  @keyframes fadeIn { from { opacity: 0; } to { opacity: 1; } }
  @keyframes scaleIn { from { opacity: 0; transform: scale(0.95); } to { opacity: 1; transform: scale(1); } }
  @keyframes slideUp { from { opacity: 0; transform: translateY(10px); } to { opacity: 1; transform: translateY(0); } }
  @keyframes panelIn { from { opacity: 0; transform: translateY(16px); } to { opacity: 1; transform: translateY(0); } }

  @media (max-width: 768px) {
    .detail-modal {
      position: fixed; top: 0; left: 0; right: 0; bottom: 0;
      max-width: 100vw; max-height: 100dvh; border-radius: 0;
    }
    .detail-modal-inner {
      flex-direction: column; border-radius: 0;
      max-height: 100dvh; height: 100dvh;
    }
    /* Banner ambient: la stessa locandina (URL identico = zero traffico
       extra) che sborda sfocata oltre i bordi: il poster sembra
       prolungarsi (stile Spotify). Vivido di proposito: niente tagli
       di luminosita, altrimenti su arte scura non si percepisce. */
    .detail-poster-col { background: #000; }
    .detail-poster-btn { position: relative; overflow: hidden; }
    .detail-poster-ambient {
      display: block; position: absolute; inset: -15%;
      width: 130%; height: 130%;
      object-fit: cover; filter: blur(32px) saturate(1.8);
    }
    .detail-poster {
      position: relative; width: 100%; height: 38dvh;
      aspect-ratio: auto; object-fit: contain;
      /* Trasparente: il suo fondo coprirebbe l'ambient dietro.
         (Su desktop resta --bg-tertiary: la colonna calza a pennello.) */
      background: transparent;
    }
    .detail-info-col { max-width: none; padding: 1rem; }
    .detail-modal :global(.detail-desc) { max-height: none; }
    .detail-modal :global(.detail-stat-path) { max-width: none; }
    .detail-modal :global(.detail-stat-url) { max-width: none; }
    /* Un solo figlio visibile per volta: mobile back a sinistra,
       desktop X a destra (space-between li metterebbe entrambi male). */
    .detail-topbar { justify-content: flex-start; }
    /* Su mobile resta solo '<- Serie': la X vive su desktop. */
    .detail-close { display: none; }
    /* Spazio sotto le azioni solo su mobile (era il bianco su desktop). */
    .detail-modal :global(.detail-actions) { padding-bottom: 96px; }
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
    .detail-poster { width: 160px; max-height: 100dvh; aspect-ratio: 2/3; object-fit: contain; }
    .detail-info-col { max-width: none; padding: 0.75rem; }
    .detail-modal :global(.detail-actions) { padding-bottom: 48px; }
  }
</style>
