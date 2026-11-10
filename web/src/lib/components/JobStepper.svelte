<script>
  // Stepper compatto del singolo episodio. Per failed/interrupted non
  // renderizza nulla: il chip + l'errore bastano, non si inventa lo step.
  let { fase } = $props();

  const STEPS = ['Coda', 'Download', 'Verifica', 'Conversione', 'Fatto'];

  // done: indici completati; current: indice attivo; il resto è pending.
  const MAPPA = {
    queued_download: { current: 0 },
    retry_wait: { current: 0 },
    downloading: { current: 1 },
    downloaded_waiting_conversion: { done: [0, 1, 2] },
    converting: { done: [0, 1, 2], current: 3 },
    completed: { done: [0, 1, 2, 3, 4] },
  };

  let stato = $derived(MAPPA[fase] || null);

  function statoStep(i) {
    if (!stato) return 'pending';
    if (stato.done?.includes(i)) return 'done';
    if (stato.current === i) return 'current';
    return 'pending';
  }
</script>

{#if stato}
  <div class="stepper" aria-hidden="true" data-fase={fase}>
    {#each STEPS as label, i (label)}
      <span class="step {statoStep(i)}" data-step={label} data-stato={statoStep(i)}>
        <span class="dot"></span>
        <span class="label">{label}</span>
      </span>
      {#if i < STEPS.length - 1}
        <span class="connector" class:done={statoStep(i) === 'done'}></span>
      {/if}
    {/each}
  </div>
{/if}

<style>
  .stepper {
    display: flex; align-items: flex-start; gap: 0.15rem;
    padding: 0.1rem 0 0.15rem;
  }
  .step {
    display: flex; flex-direction: column; align-items: center; gap: 0.25rem;
    min-width: 0; flex: 0 0 auto;
  }
  .dot {
    position: relative;
    width: 6px; height: 6px; border-radius: 50%;
    background: var(--text-muted); flex: 0 0 auto;
  }
  .label {
    font-size: 11px; line-height: 1; white-space: nowrap;
    color: var(--text-muted);
  }
  .connector {
    width: 10px; min-width: 6px; height: 1px; margin-top: 3px;
    border-radius: 1px;
    background: var(--text-muted); opacity: 0.5;
  }

  /* Step in corso: il punto respira e un anello si espande dal centro.
     Restano solo transform/box-shadow/opacity, quindi niente reflow. */
  .step.current .dot {
    background: var(--accent);
    box-shadow: 0 0 0 2px var(--accent-bg);
    animation: motion-glow calc(var(--motion-shimmer) * 2) var(--motion-ease-in-out) infinite;
  }
  .step.current .dot::after {
    content: '';
    position: absolute; inset: -2px; border-radius: 50%;
    transform: scale(0.75);
    animation: motion-ring calc(var(--motion-shimmer) * 2) var(--motion-ease-out) infinite;
  }
  .step.current .label {
    color: var(--accent-light);
    animation: motion-fade var(--motion-enter) var(--motion-ease-out);
  }

  /* Step completato: pop breve e alone success, una sola volta al cambio
     di fase; nessuna animazione infinita sui punti già fatti. */
  .step.done .dot {
    background: var(--success);
    animation: motion-pop calc(var(--motion-enter) * 1.5) var(--motion-ease-out);
  }
  .step.done .dot::before {
    content: '';
    position: absolute; inset: -3px; border-radius: 50%;
    background: var(--success); opacity: 0.16;
  }
  .step.done .label {
    color: var(--success);
    animation: motion-fade var(--motion-enter) var(--motion-ease-out);
  }

  /* Connettore completato: riflesso che attraversa la linea una volta.
     A fine corsa il gradiente torna fuori dall'elemento (200% 0), quindi
     resta solo la linea verde: nessun bagliore statico. */
  .connector.done {
    background-color: var(--success);
    background-image: linear-gradient(90deg, transparent, rgba(255, 255, 255, 0.9), transparent);
    background-size: 200% 100%;
    background-repeat: no-repeat;
    background-position: 200% 0;
    opacity: 0.8;
    animation: motion-shimmer var(--motion-shimmer) var(--motion-ease-out) 1;
  }

  @media (max-width: 480px) {
    .label { font-size: 10px; }
    .connector { width: 6px; min-width: 4px; }
  }
</style>
