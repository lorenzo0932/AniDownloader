<script>
  import { onMount } from 'svelte';
  import { api } from '../api.js';

  // `embedded`: sezione dentro Logs (niente header proprio).
  // `readonly`: sola diagnostica, nessun retry manuale.
  let { readonly = false, embedded = false } = $props();

  const RECENTE_MS = 24 * 60 * 60 * 1000;

  let items = $state([]);
  let counts = $state({ attesa: 0, attivo: 0, fatto: 0, fallito: 0 });
  let stato = $state(null);
  let loading = $state(false);
  let error = $state('');

  async function load() {
    loading = true;
    try {
      const [coda, sched] = await Promise.allSettled([
        api.queue.list({ limit: 500 }),
        api.scheduler?.status ? api.scheduler.status() : Promise.resolve(null),
      ]);
      if (coda.status === 'fulfilled') {
        const data = coda.value || {};
        items = data.items || [];
        counts = data.counts || counts;
      } else {
        throw coda.reason;
      }
      if (sched.status === 'fulfilled' && sched.value) stato = sched.value;
      error = '';
    } catch (e) {
      error = e.message || 'Errore nel caricamento dei controlli';
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

  function dataOra(iso) {
    if (!iso) return '';
    const d = new Date(iso);
    if (Number.isNaN(d.getTime())) return '';
    return d.toLocaleString('it-IT', {
      day: '2-digit', month: 'short', hour: '2-digit', minute: '2-digit',
    });
  }

  function oraEpoch(epoch) {
    if (!epoch) return '';
    return new Date(epoch * 1000).toLocaleTimeString('it-IT', {
      hour: '2-digit', minute: '2-digit',
    });
  }

  function relativo(epoch) {
    if (!epoch) return '';
    const sec = Math.round(epoch - Date.now() / 1000);
    const abs = Math.abs(sec);
    if (abs < 60) return sec >= 0 ? 'adesso' : 'poco fa';
    if (abs < 3600) return sec >= 0
      ? `tra ${Math.ceil(abs / 60)} min`
      : `${Math.floor(abs / 60)} min fa`;
    if (abs < 86400) return sec >= 0
      ? `tra ${Math.ceil(abs / 3600)} h`
      : `${Math.floor(abs / 3600)} h fa`;
    return sec >= 0
      ? `tra ${Math.ceil(abs / 86400)} g`
      : `${Math.floor(abs / 86400)} g fa`;
  }

  function prossima(epoch) {
    if (!epoch || epoch <= 0) return 'subito';
    const sec = epoch - Math.floor(Date.now() / 1000);
    if (sec <= 0) return 'subito';
    if (sec < 60) return `tra ${sec}s`;
    return `tra ${Math.ceil(sec / 60)} min`;
  }

  function etichettaKind(kind) {
    if (kind === 'check') return 'Controllo automatico';
    return kind || 'Task';
  }

  function etichettaStato(statoVoce) {
    switch (statoVoce) {
      case 'attesa': return 'In attesa';
      case 'attivo': return 'In esecuzione';
      case 'fallito': return 'Fallito';
      case 'fatto': return 'Completato';
      default: return statoVoce;
    }
  }

  function puoRiprovare(item) {
    return item.stato === 'fallito' || item.stato === 'attesa';
  }

  let falliti = $derived(items.filter((i) => i.stato === 'fallito'));
  let inCorso = $derived(items.filter((i) => i.stato === 'attivo' || i.stato === 'attesa'));
  let recenti = $derived.by(() => {
    const soglia = Date.now() - RECENTE_MS;
    return falliti.filter((i) => {
      const t = Date.parse(i.creato_il || '');
      return Number.isFinite(t) && t >= soglia;
    });
  });
  let storico = $derived.by(() => {
    const soglia = Date.now() - RECENTE_MS;
    return falliti.filter((i) => {
      const t = Date.parse(i.creato_il || '');
      return !Number.isFinite(t) || t < soglia;
    });
  });
  let recentiVisibili = $derived(recenti.slice(0, 6));
  let altriRecenti = $derived(Math.max(0, recenti.length - recentiVisibili.length));

  let statoLabel = $derived(
    !stato ? 'Non disponibile'
      : stato.inPausa ? 'In pausa'
        : stato.attivo ? 'Attivo' : 'Fermo'
  );
  let esitoLabel = $derived(
    !stato?.ultimoEsito ? 'Nessun giro ancora'
      : stato.ultimoEsito === 'ok' ? 'Ultimo giro ok'
        : 'Ultimo giro fallito'
  );

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
    return () => {
      if (sse) sse.close();
    };
  });
</script>

<div class="coda" class:embedded>
  {#if !embedded}
    <header class="coda-head">
      <div>
        <h1>Controlli</h1>
        <p class="coda-sub">Storico dei controlli periodici. Un controllo senza novità non richiede azione.</p>
      </div>
      <button class="btn-refresh" onclick={load} disabled={loading}>
        {loading ? 'Aggiorno…' : 'Aggiorna'}
      </button>
    </header>
  {:else}
    <div class="coda-tools">
      <button class="btn-refresh" onclick={load} disabled={loading}
              aria-label="Aggiorna controlli automatici">
        {loading ? 'Aggiorno…' : 'Aggiorna'}
      </button>
    </div>
  {/if}

  {#if error}
    <p class="coda-error" role="alert">{error}</p>
  {/if}

  <div class="riepilogo" aria-label="Riepilogo controlli">
    <div class="riepilogo-card">
      <span class="r-label">Ultimo controllo</span>
      <strong>{stato?.ultimoGiro ? relativo(stato.ultimoGiro) : '—'}</strong>
      <span class="r-sub">{stato?.ultimoGiro ? oraEpoch(stato.ultimoGiro) : 'nessun dato'}</span>
    </div>
    <div class="riepilogo-card">
      <span class="r-label">Prossimo</span>
      <strong>{stato?.prossimoGiro ? relativo(stato.prossimoGiro) : '—'}</strong>
      <span class="r-sub">ogni {stato?.intervalloMinuti ?? 15} min</span>
    </div>
    <div class="riepilogo-card">
      <span class="r-label">Stato</span>
      <strong class:ok={stato?.ultimoEsito === 'ok'} class:ko={stato?.ultimoEsito === 'errore'}>
        {statoLabel}
      </strong>
      <span class="r-sub">{esitoLabel}</span>
    </div>
    <div class="riepilogo-card">
      <span class="r-label">In coda</span>
      <strong>{counts.attesa + counts.attivo}</strong>
      <span class="r-sub">{counts.fatto ?? 0} completati nello storico</span>
    </div>
  </div>

  {#if stato?.ultimoMessaggio && stato.ultimoEsito === 'errore'}
    <p class="ultimo-errore" role="status">
      Ultimo errore: {stato.ultimoMessaggio}
    </p>
  {/if}

  {#if recenti.length > 0}
    <section class="sezione" aria-labelledby="titolo-problemi">
      <h2 id="titolo-problemi">Problemi recenti <span class="sezione-count">{recenti.length}</span></h2>
      <ul class="problem-list">
        {#each recentiVisibili as item, i (item.id)}
          <li class="problem" style="--i:{Math.min(i, 8)}">
            <span class="p-icon" aria-hidden="true">!</span>
            <div class="p-copy">
              <strong>{etichettaKind(item.kind)}</strong>
              <span class="p-meta">
                {#if item.serie}{item.serie} · {/if}{dataOra(item.creato_il)} · {item.tentativi} tentativi
              </span>
              {#if item.errore}
                <p class="p-errore">{item.errore}</p>
              {:else}
                <p class="p-errore muted">Motivo non disponibile per i controlli precedenti all'aggiornamento.</p>
              {/if}
            </div>
            {#if !readonly && puoRiprovare(item)}
              <button class="btn-retry" onclick={() => retry(item)}
                      aria-label={'Riprova task ' + item.id}>Riprova ora</button>
            {/if}
          </li>
        {/each}
      </ul>
      {#if altriRecenti > 0}
        <p class="altri">e altri {altriRecenti} problemi nelle ultime 24 ore.</p>
      {/if}
    </section>
  {:else if falliti.length === 0 && inCorso.length === 0}
    <p class="coda-empty">Nessun controllo in coda. I controlli completati non richiedono azione.</p>
  {/if}

  {#if inCorso.length > 0}
    <section class="sezione" aria-labelledby="titolo-coda">
      <h2 id="titolo-coda">In corso e in attesa <span class="sezione-count">{inCorso.length}</span></h2>
      <ul class="queue-list">
        {#each inCorso as item, i (item.id)}
          <li class="queue-row" style="--i:{Math.min(i, 8)}">
            <span class="q-dot" class:attivo={item.stato === 'attivo'} aria-hidden="true"></span>
            <span class="q-kind">{etichettaKind(item.kind)}</span>
            {#if item.serie}<span class="q-serie">{item.serie}</span>{/if}
            <span class="q-stato">{etichettaStato(item.stato)}</span>
            <span class="q-prossima">{item.stato === 'attivo' ? 'ora' : prossima(item.prossima_esecuzione)}</span>
            {#if !readonly && puoRiprovare(item)}
              <button class="btn-retry" onclick={() => retry(item)}
                      aria-label={'Riprova task ' + item.id}>Riprova ora</button>
            {/if}
          </li>
        {/each}
      </ul>
    </section>
  {/if}

  {#if storico.length > 0}
    <details class="storico">
      <summary>
        Storico <span class="sezione-count">{storico.length}</span>
        <span class="storico-hint">falliti prima delle ultime 24 ore</span>
      </summary>
      <ul class="problem-list storico-list">
        {#each storico as item, i (item.id)}
          <li class="problem" style="--i:{Math.min(i, 8)}">
            <span class="p-icon muted" aria-hidden="true">!</span>
            <div class="p-copy">
              <strong>{etichettaKind(item.kind)}</strong>
              <span class="p-meta">
                {#if item.serie}{item.serie} · {/if}{dataOra(item.creato_il)} · {item.tentativi} tentativi
              </span>
              {#if item.errore}
                <p class="p-errore">{item.errore}</p>
              {:else}
                <p class="p-errore muted">Motivo non disponibile (controllo precedente all'aggiornamento).</p>
              {/if}
            </div>
            {#if !readonly && puoRiprovare(item)}
              <button class="btn-retry" onclick={() => retry(item)}
                      aria-label={'Riprova task ' + item.id}>Riprova ora</button>
            {/if}
          </li>
        {/each}
      </ul>
    </details>
  {/if}
</div>

<style>
  .coda { padding: 1.5rem; max-width: var(--content-max, 1200px); margin: 0 auto; }
  .coda.embedded { padding: 0; max-width: none; }
  .coda-head { display: flex; align-items: flex-start; justify-content: space-between; gap: 1rem; }
  .coda-head h1 { font-size: 1.4rem; }
  .coda-sub { color: var(--text-muted); font-size: 12px; margin-top: 0.25rem; }
  .coda-tools { display: flex; justify-content: flex-end; margin-bottom: 0.75rem; }
  .btn-refresh {
    background: var(--bg-tertiary); color: var(--text-primary);
    border: 1px solid var(--border-color); border-radius: 8px;
    padding: 0.45rem 0.8rem; cursor: pointer; font-family: inherit;
  }
  .btn-refresh:hover:not(:disabled) { background: var(--bg-hover); }
  .btn-refresh:disabled { color: var(--text-muted); cursor: default; }

  .riepilogo {
    display: grid; grid-template-columns: repeat(auto-fit, minmax(150px, 1fr));
    gap: 0.6rem; margin: 1rem 0 0.5rem;
  }
  .riepilogo-card {
    display: flex; flex-direction: column; gap: 0.2rem;
    background: var(--bg-secondary); border: 1px solid var(--border-color);
    border-radius: 10px; padding: 0.7rem 0.8rem;
  }
  .r-label { font-size: 11px; color: var(--text-muted); text-transform: uppercase; letter-spacing: 0.04em; }
  .riepilogo-card strong { font-size: 1rem; color: var(--text-primary); }
  .riepilogo-card strong.ok { color: var(--success); }
  .riepilogo-card strong.ko { color: var(--danger); }
  .r-sub { font-size: 11px; color: var(--text-muted); }

  .ultimo-errore {
    background: var(--danger-bg); border: 1px solid var(--danger-border);
    color: var(--danger); border-radius: 8px; padding: 0.6rem 0.75rem;
    font-size: 12px; margin: 0.5rem 0 0;
  }

  .sezione { margin-top: 1.25rem; }
  .sezione h2 {
    display: flex; align-items: center; gap: 0.4rem;
    font-size: 0.8rem; font-weight: 700; text-transform: uppercase;
    letter-spacing: 0.04em; color: var(--accent-light); margin-bottom: 0.5rem;
  }
  .sezione-count {
    font-size: 10px; font-weight: 700; color: var(--text-muted);
    background: var(--bg-tertiary); border-radius: 999px; padding: 0.1rem 0.4rem;
  }

  .problem-list, .queue-list { list-style: none; display: flex; flex-direction: column; gap: 0.35rem; }
  .problem {
    display: flex; align-items: flex-start; gap: 0.55rem;
    background: var(--bg-secondary); border: 1px solid var(--border-color);
    border-left: 3px solid var(--danger); border-radius: 8px; padding: 0.55rem 0.7rem;
    animation: motion-rise var(--motion-enter) var(--motion-ease-out) backwards;
    animation-delay: calc(var(--i, 0) * var(--motion-stagger));
  }
  .p-icon {
    width: 20px; height: 20px; flex: 0 0 auto; border-radius: 50%;
    display: grid; place-items: center; font-size: 11px; font-weight: 700;
    background: var(--danger-bg); color: var(--danger); margin-top: 1px;
  }
  .p-icon.muted { background: var(--bg-tertiary); color: var(--text-muted); }
  .p-copy { flex: 1; min-width: 0; display: flex; flex-direction: column; gap: 0.15rem; }
  .p-copy strong { font-size: 13px; color: var(--text-primary); }
  .p-meta { font-size: 11px; color: var(--text-muted); }
  .p-errore { font-size: 12px; color: var(--danger); margin: 0.1rem 0 0; }
  .p-errore.muted { color: var(--text-muted); }
  .altri { font-size: 11px; color: var(--text-muted); margin-top: 0.4rem; }

  .queue-row {
    display: flex; align-items: center; gap: 0.55rem;
    background: var(--bg-secondary); border: 1px solid var(--border-color);
    border-radius: 8px; padding: 0.5rem 0.7rem; font-size: 12px;
    animation: motion-rise var(--motion-enter) var(--motion-ease-out) backwards;
    animation-delay: calc(var(--i, 0) * var(--motion-stagger));
  }
  .q-dot { width: 8px; height: 8px; border-radius: 50%; background: var(--warning); flex: 0 0 auto; }
  .q-dot.attivo { background: var(--accent); animation: motion-pulse 1s ease-in-out infinite; }
  .q-kind { font-weight: 600; color: var(--text-primary); }
  .q-serie { color: var(--text-secondary); }
  .q-stato { color: var(--text-muted); }
  .q-prossima { margin-left: auto; color: var(--text-muted); font-variant-numeric: tabular-nums; }

  .storico {
    margin-top: 1.25rem; border-top: 1px solid var(--border-color); padding-top: 0.75rem;
  }
  .storico summary {
    cursor: pointer; color: var(--text-secondary); font-size: 12px;
    display: flex; align-items: center; gap: 0.4rem; flex-wrap: wrap;
  }
  .storico-hint { color: var(--text-muted); }
  .storico-list { margin-top: 0.6rem; }
  .storico .problem { border-left-color: var(--border-color); }

  .btn-retry {
    background: none; border: 1px solid var(--border-color);
    color: var(--accent-light); border-radius: 6px;
    padding: 0.3rem 0.55rem; cursor: pointer; font-size: 11px;
    white-space: nowrap; font-family: inherit;
  }
  .btn-retry:hover { background: var(--bg-hover); }
  .coda-empty { color: var(--text-muted); margin-top: 1rem; font-size: 13px; }
  .coda-error { color: var(--danger); margin: 0.75rem 0; }

  @media (max-width: 640px) {
    .problem { flex-wrap: wrap; }
    .problem .btn-retry { margin-left: 2.2rem; }
  }
</style>
