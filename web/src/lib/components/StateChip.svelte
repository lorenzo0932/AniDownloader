<script>
  // Unica fonte delle etichette e dei colori di stato di un episodio.
  // `retry_wait` non resta mai "In attesa" da solo: se il backend espone
  // `prossima_esecuzione` (epoch secondi) mostra l'orario locale.
  let { fase, prossimaEsecuzione = 0 } = $props();

  const ETICHETTE = {
    queued_download: 'In coda',
    downloading: 'In download',
    downloaded_waiting_conversion: 'In attesa conversione',
    converting: 'In conversione',
    failed: 'Non riuscito',
    completed: 'Completato',
    interrupted: 'Interrotto',
  };

  function orario(epoch) {
    const sec = Number(epoch);
    if (!Number.isFinite(sec) || sec <= 0) return '';
    return new Date(sec * 1000).toLocaleTimeString('it-IT', {
      hour: '2-digit',
      minute: '2-digit',
    });
  }

  let etichetta = $derived.by(() => {
    if (fase === 'retry_wait') {
      const ora = orario(prossimaEsecuzione);
      return ora ? `In attesa · riprovo alle ${ora}` : 'In attesa · riprovo a breve';
    }
    return ETICHETTE[fase] || fase || '';
  });
</script>

<span class="job-stato stato-{fase}" data-fase={fase}>{etichetta}</span>

<style>
  .job-stato {
    font-size: 12px; padding: 0.15rem 0.5rem; border-radius: 999px;
    white-space: nowrap;
  }
  .stato-queued_download, .stato-retry_wait {
    background: var(--warning-bg); color: var(--warning-text);
  }
  .stato-downloading, .stato-converting {
    background: var(--accent-bg); color: var(--accent-light);
  }
  .stato-downloaded_waiting_conversion {
    background: var(--bg-tertiary); color: var(--text-secondary);
  }
  .stato-failed { background: var(--danger-bg); color: var(--danger); }
  /* Completato è lo stato normale: testo discreto, non una pill ripetuta
     su ogni riga. Gli stati che richiedono attenzione restano pill. */
  .stato-completed {
    background: transparent; color: var(--success); padding: 0;
    font-weight: 600;
  }
  .stato-interrupted { background: var(--bg-tertiary); color: var(--text-muted); }
</style>
