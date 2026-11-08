<script>
  import { onMount } from 'svelte';
  import { api } from '../api.js';

  let items = $state([]);
  let counts = $state({ attesa: 0, attivo: 0, fatto: 0, fallito: 0 });
  let loading = $state(false);
  let error = $state('');

  async function load() {
    loading = true;
    try {
      const data = await api.queue.list();
      items = data.items || [];
      counts = data.counts || counts;
      error = '';
    } catch (e) {
      error = e.message;
    } finally {
      loading = false;
    }
  }

  async function retry(item) {
    try {
      await api.queue.retry(item.id);
      await load();
    } catch (e) {
      error = e.message;
    }
  }

  function prossima(epoch) {
    if (!epoch || epoch <= 0) return 'subito';
    const sec = epoch - Math.floor(Date.now() / 1000);
    if (sec <= 0) return 'subito';
    if (sec < 60) return `tra ${sec}s`;
    return `tra ${Math.ceil(sec / 60)} min`;
  }

  function etichettaStato(stato) {
    switch (stato) {
      case 'attesa': return 'In attesa';
      case 'attivo': return 'In esecuzione';
      case 'fallito': return 'Fallito';
      case 'fatto': return 'Completato';
      default: return stato;
    }
  }

  function puoRiprovare(item) {
    return item.stato === 'fallito' || item.stato === 'attesa';
  }

  onMount(() => {
    load();
    let sse = null;
    try {
      sse = api.sse();
      sse.onmessage = (event) => {
        try {
          const data = JSON.parse(event.data);
          if (data.type === 'queue') load();
        } catch {
          /* evento non JSON: ignora */
        }
      };
    } catch {
      sse = null;
    }
    // I "tra N min" non hanno bisogno di un timer: si aggiornano a ogni
    // evento coda e a ogni refresh manuale, che è quando cambiano davvero.
    return () => {
      if (sse) sse.close();
    };
  });
</script>

<div class="coda">
  <header class="coda-head">
    <h1>Coda</h1>
    <button class="btn-refresh" onclick={load} disabled={loading}>
      {loading ? 'Aggiorno…' : 'Aggiorna'}
    </button>
  </header>

  <div class="coda-chips" aria-label="Conteggi coda">
    <span class="chip">In attesa <strong>{counts.attesa}</strong></span>
    <span class="chip">In esecuzione <strong>{counts.attivo}</strong></span>
    <span class="chip chip-ko">Falliti <strong>{counts.fallito}</strong></span>
    <span class="chip chip-ok">Completati <strong>{counts.fatto}</strong></span>
  </div>

  {#if error}
    <p class="coda-error">{error}</p>
  {/if}

  {#if items.length === 0}
    <p class="coda-empty">Coda vuota.</p>
  {:else}
    <div class="coda-table-wrap">
      <table class="coda-table">
        <thead>
          <tr>
            <th>Tipo</th>
            <th>Serie</th>
            <th>Ep.</th>
            <th>Stato</th>
            <th>Tentativi</th>
            <th>Prossima</th>
            <th></th>
          </tr>
        </thead>
        <tbody>
          {#each items as item (item.id)}
            <tr>
              <td class="mono">{item.kind}</td>
              <td>{item.serie || '—'}</td>
              <td class="mono">{item.episodio || '—'}</td>
              <td><span class="stato stato-{item.stato}">{etichettaStato(item.stato)}</span></td>
              <td class="mono">{item.tentativi}</td>
              <td>{prossima(item.prossima_esecuzione)}</td>
              <td>
                {#if puoRiprovare(item)}
                  <button class="btn-retry" onclick={() => retry(item)}
                          aria-label={'Riprova task ' + item.id}>
                    Riprova ora
                  </button>
                {/if}
              </td>
            </tr>
          {/each}
        </tbody>
      </table>
    </div>
  {/if}
</div>

<style>
  .coda { padding: 1.5rem; max-width: 1100px; margin: 0 auto; }
  .coda-head { display: flex; align-items: center; justify-content: space-between; gap: 1rem; }
  .coda-head h1 { font-size: 1.4rem; }
  .btn-refresh {
    background: var(--bg-tertiary); color: var(--text-primary);
    border: 1px solid var(--border-color); border-radius: 8px;
    padding: 0.45rem 0.8rem; cursor: pointer;
  }
  .btn-refresh:hover:not(:disabled) { background: var(--bg-hover); }
  .btn-refresh:disabled { color: var(--text-muted); cursor: default; }
  .coda-chips { display: flex; flex-wrap: wrap; gap: 0.5rem; margin: 1rem 0; }
  .chip {
    padding: 0.3rem 0.6rem; border-radius: 999px; font-size: 12px;
    background: var(--bg-tertiary); color: var(--text-secondary);
    border: 1px solid var(--border-color);
  }
  .chip strong { color: var(--text-primary); margin-left: 0.25rem; }
  .chip-ko strong { color: var(--danger); }
  .chip-ok strong { color: var(--success); }
  .coda-error { color: var(--danger); margin: 0.75rem 0; }
  .coda-empty { color: var(--text-muted); margin-top: 1rem; }
  .coda-table-wrap { overflow-x: auto; }
  .coda-table {
    width: 100%; border-collapse: collapse; font-size: 13px;
    background: var(--bg-secondary); border: 1px solid var(--border-color);
    border-radius: 10px; overflow: hidden;
  }
  .coda-table th, .coda-table td {
    padding: 0.55rem 0.7rem; text-align: left;
    border-bottom: 1px solid var(--border-color);
  }
  .coda-table th { color: var(--text-muted); font-weight: 600; }
  .coda-table tbody tr:hover { background: var(--bg-hover); }
  .mono { font-variant-numeric: tabular-nums; }
  .stato { padding: 0.15rem 0.5rem; border-radius: 999px; font-size: 12px; }
  .stato-attesa { background: var(--warning-bg); color: var(--warning-text); }
  .stato-attivo { background: var(--accent-bg); color: var(--accent-light); }
  .stato-fallito { background: var(--danger-bg); color: var(--danger); }
  .stato-fatto { background: var(--bg-tertiary); color: var(--success); }
  .btn-retry {
    background: none; border: 1px solid var(--border-color);
    color: var(--accent-light); border-radius: 6px;
    padding: 0.25rem 0.5rem; cursor: pointer; font-size: 12px;
  }
  .btn-retry:hover { background: var(--bg-hover); }
</style>
