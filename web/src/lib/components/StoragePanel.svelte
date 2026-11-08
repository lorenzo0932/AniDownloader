<script>
  import { onMount } from 'svelte';
  import { api } from '../api.js';
  import ConfirmModal from './ConfirmModal.svelte';

  let dati = $state(null);
  let loading = $state(false);
  let error = $state('');
  let esito = $state('');
  let conferma = $state(null);

  function fmt(bytes) {
    const n = Number(bytes) || 0;
    if (n < 1024) return `${n} B`;
    const unita = ['KB', 'MB', 'GB', 'TB'];
    let v = n / 1024;
    let i = 0;
    while (v >= 1024 && i < unita.length - 1) {
      v /= 1024;
      i += 1;
    }
    return `${v.toFixed(1)} ${unita[i]}`;
  }

  async function load() {
    loading = true;
    try {
      dati = await api.storage.info();
      error = '';
    } catch (e) {
      error = e.message;
    } finally {
      loading = false;
    }
  }

  function chiedi(azione, titolo, messaggio) {
    esito = '';
    conferma = { azione, titolo, messaggio };
  }

  async function esegui() {
    const azione = conferma?.azione;
    conferma = null;
    try {
      const r = await api.storage.cleanup(azione);
      esito = `Rimossi ${r.removedFiles} file (${fmt(r.freedBytes)}).`;
      await load();
    } catch (e) {
      error = e.message;
    }
  }

  onMount(load);
</script>

<section class="spazio" aria-label="Spazio e pulizia">
  <header class="spazio-head">
    <h2>Spazio e pulizia</h2>
    <button class="btn-refresh" onclick={load} disabled={loading}>
      {loading ? 'Calcolo…' : 'Aggiorna'}
    </button>
  </header>

  {#if error}
    <p class="spazio-error">{error}</p>
  {/if}
  {#if esito}
    <p class="spazio-ok">{esito}</p>
  {/if}

  {#if dati}
    <div class="spazio-cards">
      <div class="spazio-card">
        <span class="label">Disco (output)</span>
        <strong>{fmt(dati.freeBytes)}</strong>
        <span class="hint">liberi su {fmt(dati.totalBytes)}</span>
      </div>
      <div class="spazio-card">
        <span class="label">Cache miniature</span>
        <strong>{fmt(dati.cache?.bytes)}</strong>
        <span class="hint">{dati.cache?.files ?? 0} file</span>
        <button class="btn-azione" onclick={() =>
          chiedi('cache', 'Svuotare la cache miniature?',
                 'Le miniature verranno rigenerate alla prossima richiesta.')}>
          Svuota cache
        </button>
      </div>
      <div class="spazio-card">
        <span class="label">File parziali</span>
        <strong>{fmt(dati.partial?.bytes)}</strong>
        <span class="hint">{dati.partial?.files ?? 0} residui (.aria2/.part)</span>
        <button class="btn-azione" onclick={() =>
          chiedi('parziali', 'Rimuovere i file parziali?',
                 'Vengono rimossi solo i residui .aria2/.part; i video non si toccano.')}>
          Rimuovi parziali
        </button>
      </div>
    </div>

    {#if (dati.series || []).length > 0}
      <h3 class="spazio-sub">Per serie</h3>
      <ul class="spazio-lista">
        {#each dati.series as s (s.name + s.path)}
          <li>
            <span class="serie-nome">{s.name}</span>
            <span class="serie-bytes">{fmt(s.bytes)}</span>
            {#if s.partialFiles > 0}
              <span class="serie-parziali">{s.partialFiles} parziali</span>
            {/if}
          </li>
        {/each}
      </ul>
    {/if}
  {/if}
</section>

<ConfirmModal show={!!conferma} title={conferma?.titolo || 'Conferma'}
              message={conferma?.messaggio || ''} confirmText="Conferma"
              danger={false} onConfirm={esegui} onCancel={() => (conferma = null)} />

<style>
  .spazio {
    margin-top: 1.5rem; padding: 1rem; border: 1px solid var(--border-color);
    border-radius: 10px; background: var(--bg-secondary);
  }
  .spazio-head { display: flex; align-items: center; justify-content: space-between; }
  .spazio-head h2 { font-size: 1.05rem; }
  .btn-refresh {
    background: var(--bg-tertiary); color: var(--text-primary);
    border: 1px solid var(--border-color); border-radius: 6px;
    padding: 0.3rem 0.6rem; cursor: pointer; font-size: 12px;
  }
  .btn-refresh:disabled { color: var(--text-muted); cursor: default; }
  .spazio-error { color: var(--danger); margin-top: 0.6rem; }
  .spazio-ok { color: var(--success); margin-top: 0.6rem; }
  .spazio-cards {
    display: grid; grid-template-columns: repeat(auto-fit, minmax(180px, 1fr));
    gap: 0.75rem; margin-top: 0.9rem;
  }
  .spazio-card {
    display: flex; flex-direction: column; gap: 0.25rem;
    padding: 0.75rem; border: 1px solid var(--border-color); border-radius: 8px;
  }
  .spazio-card .label { color: var(--text-muted); font-size: 12px; }
  .spazio-card strong { font-size: 1.1rem; }
  .hint { color: var(--text-secondary); font-size: 12px; }
  .btn-azione {
    margin-top: 0.4rem; align-self: flex-start;
    background: none; border: 1px solid var(--border-color); border-radius: 6px;
    color: var(--accent-light); padding: 0.25rem 0.5rem; cursor: pointer;
    font-size: 12px;
  }
  .btn-azione:hover { background: var(--bg-hover); }
  .spazio-sub { margin: 1.1rem 0 0.4rem; font-size: 0.95rem; }
  .spazio-lista { list-style: none; margin: 0; padding: 0; }
  .spazio-lista li {
    display: flex; align-items: baseline; gap: 0.6rem;
    padding: 0.35rem 0; border-bottom: 1px solid var(--border-color);
    font-size: 13px;
  }
  .serie-nome { flex: 1; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
  .serie-bytes { color: var(--text-secondary); font-variant-numeric: tabular-nums; }
  .serie-parziali { color: var(--warning-text); font-size: 12px; }
</style>
