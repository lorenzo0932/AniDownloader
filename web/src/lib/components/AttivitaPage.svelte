<script>
  import { onMount } from 'svelte';
  import { pageEnter } from '../motion.js';
  import { api, posterUrl, posterSrcSet } from '../api.js';

  // Fasi che compongono "In corso": tutto ciò che non è ancora completato.
  // `interrupted` resta visibile qui invece di sparire da entrambe le tab.
  const FASI_CORSO = new Set([
    'queued_download', 'downloading', 'downloaded_waiting_conversion',
    'converting', 'retry_wait', 'failed', 'interrupted',
  ]);

  const ETICHETTE = {
    queued_download: 'In attesa',
    downloading: 'In download',
    downloaded_waiting_conversion: 'In attesa conversione',
    converting: 'In conversione',
    retry_wait: 'In attesa',
    failed: 'Errore',
    completed: 'Completato',
    interrupted: 'Interrotto',
  };

  const FILTRI = [
    ['tutti', 'Tutti'],
    ['corso', 'In corso'],
    ['errori', 'Errori'],
  ];

  // Le ultime 24 ore separano "Recenti" (attivo + appena concluso) dallo
  // Storico (concluso da più di 24h).
  const FINESTRA_RECENTI_MS = 24 * 60 * 60 * 1000;

  let items = $state([]);
  let tab = $state('recenti'); // recenti | storico
  let filtro = $state('tutti');
  let aperti = $state({});
  let loading = $state(false);
  let error = $state('');

  async function load() {
    loading = true;
    try {
      const data = await api.pipeline.list({ limit: 200 });
      items = data.items || [];
      error = '';
    } catch (e) {
      error = e.message;
    } finally {
      loading = false;
    }
  }

  function scegliTab(t) {
    tab = t;
    filtro = 'tutti';
    aperti = {};
  }

  function scegliFiltro(f) {
    // I filtri raffinano solo la tab "In corso": lo Storico è per definizione
    // completato, quindi non ha filtri di stato.
    filtro = f;
  }

  function inCorso(j) {
    return FASI_CORSO.has(j.fase);
  }

  function recente(j) {
    if (inCorso(j)) return true;
    const t = Date.parse(j.aggiornato_il || j.creato_il || '');
    return Number.isFinite(t) && Date.now() - t < FINESTRA_RECENTI_MS;
  }

  function passaFiltro(j) {
    if (filtro === 'corso') return inCorso(j) && j.fase !== 'failed';
    if (filtro === 'errori') return j.fase === 'failed';
    return true;
  }

  let visibili = $derived(
    items
      .filter(j => (tab === 'storico' ? !recente(j) : recente(j)))
      .filter(passaFiltro)
  );

  function etichetta(fase) {
    return ETICHETTE[fase] || fase;
  }

  function titolo(j) {
    const ep = j.episodio ? `Ep. ${j.episodio}` : 'episodio';
    return `${j.serie || 'Serie sconosciuta'} · ${ep}`;
  }

  function toggle(id) {
    aperti = { ...aperti, [id]: !aperti[id] };
  }

  // Tempo relativo leggibile dal timestamp ISO del backend.
  function tempoRelativo(iso) {
    if (!iso) return '';
    const t = Date.parse(iso);
    if (Number.isNaN(t)) return '';
    const sec = Math.max(0, Math.floor((Date.now() - t) / 1000));
    if (sec < 60) return 'adesso';
    if (sec < 3600) return `${Math.floor(sec / 60)} min fa`;
    if (sec < 86400) return `${Math.floor(sec / 3600)} h fa`;
    return `${Math.floor(sec / 86400)} g fa`;
  }

  function origine(j) {
    return j.origine === 'interactive' ? 'manuale' : 'automatico';
  }

  async function riprova(j) {
    error = '';
    try {
      await api.pipeline.retry(j.id);
      await load();
    } catch (e) {
      error = e.message;
    }
  }

  onMount(() => {
    load();
    // Nessun evento SSE dedicato alla pipeline: polling leggero condiviso
    // con la vecchia pagina Pipeline.
    const timer = setInterval(load, 5000);
    return () => clearInterval(timer);
  });
</script>

<div class="attivita" in:pageEnter|global>
  <header class="head">
    <h1>Attività</h1>
    <button class="btn-refresh" onclick={load} disabled={loading} aria-label="Aggiorna attività">
      {loading ? 'Aggiorno…' : 'Aggiorna'}
    </button>
  </header>

  <div class="tabs" role="tablist" aria-label="Attività">
    <button role="tab" id="tab-recenti" aria-selected={tab === 'recenti'}
            aria-controls="panel-attivita" class:active={tab === 'corso'}
            onclick={() => scegliTab('recenti')}>Recenti</button>
    <button role="tab" id="tab-storico" aria-selected={tab === 'storico'}
            aria-controls="panel-attivita" class:active={tab === 'storico'}
            onclick={() => scegliTab('storico')}>Storico</button>
  </div>

  {#if tab === 'recenti'}
    <div class="filtri" role="group" aria-label="Filtra attività recenti">
      {#each FILTRI as [valore, label] (valore)}
        <button class="chip" aria-pressed={filtro === valore}
                class:attivo={filtro === valore}
                onclick={() => scegliFiltro(valore)}>{label}</button>
      {/each}
    </div>
  {/if}

  {#if error}
    <p class="err" role="alert">{error}</p>
  {/if}

  <p class="finestra">
    {tab === 'recenti'
      ? 'Attività in corso e concluse nelle ultime 24 ore.'
      : 'Concluse da più di 24 ore.'}
  </p>

  <div id="panel-attivita" role="tabpanel" aria-labelledby={tab === 'recenti' ? 'tab-recenti' : 'tab-storico'}>
    {#if visibili.length === 0}
      <p class="empty">
        {tab === 'storico' ? 'Nessun episodio nello storico.' : 'Nessuna attività recente.'}
      </p>
    {:else}
      <ul class="job-list">
        {#each visibili as j, i (j.id)}
          <li class="job" class:job-failed={j.fase === 'failed'}
              style="--i:{Math.min(i, 8)}">
            <button class="job-main" onclick={() => toggle(j.id)}
                    aria-expanded={aperti[j.id] ? 'true' : 'false'}
                    aria-label={`Dettagli di ${titolo(j)}`}>
              {#if j.path}
                <img class="job-poster" src={posterUrl(j.path, 96)}
                     srcset={posterSrcSet(j.path)} sizes="36px" alt=""
                     decoding="async" width="36" height="50" />
              {:else}
                <span class="job-poster job-poster-empty" aria-hidden="true"></span>
              {/if}
              <span class="job-title">{titolo(j)}</span>
              <span class="job-stato stato-{j.fase}">{etichetta(j.fase)}</span>
              <span class="job-tempo">{tempoRelativo(j.aggiornato_il)}</span>
            </button>

            {#if aperti[j.id]}
              <div class="job-details">
                <dl>
                  <div>
                    <dt>Avviato da</dt>
                    <dd>{origine(j)}</dd>
                  </div>
                  <div>
                    <dt>Tentativi</dt>
                    <dd class="mono">{j.tentativi ?? 0}</dd>
                  </div>
                  {#if j.errore}
                    <div>
                      <dt>Errore</dt>
                      <dd class="errore">{j.errore}</dd>
                    </div>
                  {/if}
                </dl>
                {#if j.fase === 'failed'}
                  <button class="btn-retry" onclick={() => riprova(j)}
                          aria-label={`Riprova ${titolo(j)}`}>Riprova</button>
                {/if}
              </div>
            {/if}
          </li>
        {/each}
      </ul>
    {/if}
  </div>
</div>

<style>
  .attivita { padding: 1.5rem; max-width: 900px; margin: 0 auto; }
  .head { display: flex; align-items: center; justify-content: space-between; gap: 1rem; }
  .head h1 { font-size: 1.4rem; }
  .btn-refresh {
    background: var(--bg-tertiary); color: var(--text-primary);
    border: 1px solid var(--border-color); border-radius: 8px;
    padding: 0.45rem 0.8rem; cursor: pointer;
  }
  .btn-refresh:hover:not(:disabled) { background: var(--bg-hover); }
  .btn-refresh:disabled { color: var(--text-muted); cursor: default; }

  .tabs { display: flex; gap: 0.25rem; margin: 1rem 0 0.75rem; border-bottom: 1px solid var(--border-color); }
  .tabs button {
    background: none; border: none; border-bottom: 2px solid transparent;
    color: var(--text-secondary); padding: 0.5rem 0.9rem; cursor: pointer;
    font-size: 0.9rem; border-radius: 6px 6px 0 0;
  }
  .tabs button:hover { background: var(--bg-tertiary); color: var(--text-primary); }
  .tabs button.active { color: var(--accent-light); border-bottom-color: var(--accent); }
  .tabs button:focus-visible, .chip:focus-visible, .job-main:focus-visible,
  .btn-retry:focus-visible, .btn-refresh:focus-visible {
    outline: 2px solid var(--accent); outline-offset: 2px;
  }

  .finestra { color: var(--text-muted); font-size: 12px; margin: -0.25rem 0 0.75rem; }
  .filtri { display: flex; flex-wrap: wrap; gap: 0.5rem; margin-bottom: 1rem; }
  .chip {
    padding: 0.3rem 0.7rem; border-radius: 999px; font-size: 12px; cursor: pointer;
    background: var(--bg-tertiary); color: var(--text-secondary);
    border: 1px solid var(--border-color);
  }
  .chip:hover { background: var(--bg-hover); color: var(--text-primary); }
  .chip.attivo { background: var(--accent-bg); color: var(--accent-light); border-color: var(--accent); }

  .err { color: var(--danger); margin: 0.75rem 0; }
  .empty { color: var(--text-muted); margin-top: 1rem; }

  .job-list { list-style: none; display: flex; flex-direction: column; gap: 0.35rem; }
  .job {
    animation: motion-rise var(--motion-enter) var(--motion-ease-out) backwards;
    animation-delay: calc(var(--i, 0) * var(--motion-stagger));
    background: var(--bg-secondary); border: 1px solid var(--border-color);
    border-radius: 10px; overflow: hidden;
  }
  .job-failed { border-color: var(--danger-border); }
  .job-main {
    width: 100%; display: flex; align-items: center; gap: 0.75rem;
    background: none; border: none; color: var(--text-primary);
    padding: 0.65rem 0.8rem; text-align: left; cursor: pointer; font-size: 0.9rem;
  }
  .job-main:hover { background: var(--bg-hover); }
  .job-poster {
    width: 36px; height: 50px; object-fit: cover; border-radius: 6px;
    flex: 0 0 auto; background: var(--poster-frame-bg, var(--bg-tertiary));
    border: 1px solid var(--border-color);
  }
  .job-title { flex: 1; min-width: 0; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
  .job-tempo { color: var(--text-muted); font-size: 12px; white-space: nowrap; }
  .job-stato { font-size: 12px; padding: 0.15rem 0.5rem; border-radius: 999px; white-space: nowrap; }
  .stato-queued_download, .stato-retry_wait { background: var(--warning-bg); color: var(--warning-text); }
  .stato-downloading, .stato-converting { background: var(--accent-bg); color: var(--accent-light); }
  .stato-downloaded_waiting_conversion { background: var(--bg-tertiary); color: var(--text-secondary); }
  .stato-failed { background: var(--danger-bg); color: var(--danger); }
  .stato-completed { background: var(--bg-tertiary); color: var(--success); }
  .stato-interrupted { background: var(--bg-tertiary); color: var(--text-muted); }

  .job-details {
    border-top: 1px solid var(--border-color);
    padding: 0.7rem 0.8rem; display: flex; align-items: flex-start;
    justify-content: space-between; gap: 1rem; flex-wrap: wrap;
  }
  dl { display: flex; flex-direction: column; gap: 0.35rem; margin: 0; }
  dl > div { display: flex; gap: 0.5rem; font-size: 12px; }
  dt { color: var(--text-muted); min-width: 70px; }
  dd { margin: 0; color: var(--text-secondary); overflow-wrap: anywhere; }
  .mono { font-variant-numeric: tabular-nums; }
  .errore { color: var(--danger); }
  .btn-retry {
    background: none; border: 1px solid var(--border-color);
    color: var(--accent-light); border-radius: 6px;
    padding: 0.35rem 0.7rem; cursor: pointer; font-size: 12px;
  }
  .btn-retry:hover { background: var(--bg-hover); }

  @media (max-width: 768px) {
    .attivita { padding: 1rem; }
    .job-tempo { display: none; }
  }
</style>
