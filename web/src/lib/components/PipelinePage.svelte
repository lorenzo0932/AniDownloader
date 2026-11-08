<script>
  import { onMount } from 'svelte';
  import { api } from '../api.js';

  let items = $state([]);
  let counts = $state({});
  let error = $state('');
  let loading = $state(false);

  const FASI = [
    ['queued_download', 'In coda'],
    ['downloading', 'Download'],
    ['downloaded_waiting_conversion', 'Attesa conversione'],
    ['converting', 'Conversione'],
    ['completed', 'Completati'],
    ['retry_wait', 'Retry'],
    ['failed', 'Falliti'],
    ['interrupted', 'Interrotti'],
  ];

  function etichetta(fase) {
    const found = FASI.find(([k]) => k === fase);
    return found ? found[1] : fase;
  }

  async function load() {
    loading = true;
    try {
      const data = await api.pipeline.list({ limit: 200 });
      items = data.items || [];
      counts = data.counts || {};
      error = '';
    } catch (e) {
      error = e.message;
    } finally {
      loading = false;
    }
  }

  function prossima(epoch) {
    if (!epoch || epoch <= 0) return 'subito';
    const sec = epoch - Math.floor(Date.now() / 1000);
    return sec <= 0 ? 'subito' : `tra ${Math.ceil(sec / 60)} min`;
  }

  onMount(() => {
    load();
    // Nessun evento SSE dedicato alla pipeline: un refresh periodico leggero
    // è sufficiente e non duplica lo stato dei job nel client.
    const timer = setInterval(load, 5000);
    return () => clearInterval(timer);
  });
</script>

<div class="pipeline">
  <header class="head">
    <h1>Pipeline</h1>
    <button class="btn-refresh" onclick={load} disabled={loading}>
      {loading ? 'Aggiorno…' : 'Aggiorna'}
    </button>
  </header>

  <div class="chips" aria-label="Conteggi pipeline">
    {#each FASI as [fase, label] (fase)}
      <span class="chip" class:chip-ko={fase === 'failed'} class:chip-ok={fase === 'completed'}>
        {label} <strong>{counts[fase] ?? 0}</strong>
      </span>
    {/each}
  </div>

  {#if error}
    <p class="err">{error}</p>
  {/if}

  <p class="nota">
    Coda per-episodio persistente (JobKey = identità serie + episodio).
    L'esecuzione dei job sarà collegata nella fase di integrazione.
  </p>

  {#if items.length === 0}
    <p class="empty">Nessun job in coda.</p>
  {:else}
    <div class="table-wrap">
      <table>
        <thead>
          <tr>
            <th>Serie</th>
            <th>Ep.</th>
            <th>Origine</th>
            <th>Fase</th>
            <th>Tentativi</th>
            <th>Prossima</th>
            <th>Errore</th>
          </tr>
        </thead>
        <tbody>
          {#each items as j (j.id)}
            <tr>
              <td class="serie" title={j.job_key}>{j.serie || '—'}</td>
              <td class="mono">{j.episodio || '—'}</td>
              <td>
                <span class="origine" class:interattivo={j.origine === 'interactive'}>
                  {j.origine === 'interactive' ? 'interattivo' : 'background'}
                </span>
              </td>
              <td><span class="fase fase-{j.fase}">{etichetta(j.fase)}</span></td>
              <td class="mono">{j.tentativi}</td>
              <td>{prossima(j.prossima_esecuzione)}</td>
              <td class="err-cell">{j.errore || ''}</td>
            </tr>
          {/each}
        </tbody>
      </table>
    </div>
  {/if}
</div>

<style>
  .pipeline { padding: 1.5rem; max-width: 1100px; margin: 0 auto; }
  .head { display: flex; align-items: center; justify-content: space-between; gap: 1rem; }
  .head h1 { font-size: 1.4rem; }
  .btn-refresh {
    background: var(--bg-tertiary); color: var(--text-primary);
    border: 1px solid var(--border-color); border-radius: 8px;
    padding: 0.45rem 0.8rem; cursor: pointer;
  }
  .btn-refresh:disabled { color: var(--text-muted); cursor: default; }
  .chips { display: flex; flex-wrap: wrap; gap: 0.5rem; margin: 1rem 0; }
  .chip {
    padding: 0.3rem 0.6rem; border-radius: 999px; font-size: 12px;
    background: var(--bg-tertiary); color: var(--text-secondary);
    border: 1px solid var(--border-color);
  }
  .chip strong { color: var(--text-primary); margin-left: 0.25rem; }
  .chip-ko strong { color: var(--danger); }
  .chip-ok strong { color: var(--success); }
  .nota { color: var(--text-muted); font-size: 12px; margin: 0.5rem 0 1rem; }
  .err { color: var(--danger); margin: 0.5rem 0; }
  .empty { color: var(--text-muted); }
  .table-wrap { overflow-x: auto; }
  table {
    width: 100%; border-collapse: collapse; font-size: 13px;
    background: var(--bg-secondary); border: 1px solid var(--border-color);
    border-radius: 10px; overflow: hidden;
  }
  th, td { padding: 0.55rem 0.7rem; text-align: left; border-bottom: 1px solid var(--border-color); }
  th { color: var(--text-muted); font-weight: 600; }
  tbody tr:hover { background: var(--bg-hover); }
  .mono { font-variant-numeric: tabular-nums; }
  .serie { max-width: 240px; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
  .origine {
    font-size: 11px; padding: 0.15rem 0.45rem; border-radius: 999px;
    background: var(--bg-tertiary); color: var(--text-secondary);
  }
  .origine.interattivo { background: var(--accent-bg); color: var(--accent-light); }
  .fase { font-size: 12px; }
  .fase-failed { color: var(--danger); }
  .fase-completed { color: var(--success); }
  .fase-converting, .fase-downloading { color: var(--warning-text); }
  .err-cell { max-width: 220px; overflow: hidden; text-overflow: ellipsis; white-space: nowrap;
              color: var(--danger); }
</style>
