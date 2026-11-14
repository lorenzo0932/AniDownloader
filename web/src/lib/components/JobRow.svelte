<script>
  // Riga episodio: locandina lazy, titolo, chip di stato, stepper per gli
  // stati attivi e dettagli espandibili. Il job_key non viene mai mostrato.
  import StateChip from './StateChip.svelte';
  import JobStepper from './JobStepper.svelte';
  import { posterUrl, posterSrcSet } from '../api.js';

  let { job, expanded = false, onToggle, onRetry, poster = '' } = $props();

  let posterError = $state(false);
  $effect(() => {
    job.path;
    posterError = false;
  });

  // Lo stepper compare solo mentre il job è davvero in lavorazione.
  const FASI_ATTIVE = new Set([
    'downloading', 'converting', 'downloaded_waiting_conversion',
  ]);

  let titolo = $derived(
    `${job.serie || 'Serie sconosciuta'} · ${job.episodio ? `Ep. ${job.episodio}` : 'episodio'}`
  );
  let attiva = $derived(FASI_ATTIVE.has(job.fase));
  let src = $derived(poster || posterUrl(job.path, 96));
  let srcset = $derived(poster ? '' : posterSrcSet(job.path));
  let origine = $derived(job.origine === 'interactive' ? 'manuale' : 'automatico');
  let iniziale = $derived((job.serie || '?').trim().charAt(0).toUpperCase() || '?');

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

  function orario(epoch) {
    const sec = Number(epoch);
    if (!Number.isFinite(sec) || sec <= 0) return '';
    return new Date(sec * 1000).toLocaleTimeString('it-IT', {
      hour: '2-digit',
      minute: '2-digit',
    });
  }
</script>

<li class="job" class:job-failed={job.fase === 'failed'} data-job-id={job.id}>
  <button class="job-main" onclick={() => onToggle?.(job)}
          aria-expanded={expanded ? 'true' : 'false'}
          aria-label={`Dettagli di ${titolo}`}>
    {#if src && !posterError}
      <img class="job-poster" {src} srcset={srcset || undefined} sizes="40px"
           alt="" loading="lazy" decoding="async" width="40" height="56"
           onerror={() => (posterError = true)} />
    {:else}
      <span class="job-poster job-poster-empty" aria-hidden="true">{iniziale}</span>
    {/if}
    <span class="job-title">{titolo}</span>
    <StateChip fase={job.fase} prossimaEsecuzione={job.prossima_esecuzione} />
    <span class="job-tempo">{tempoRelativo(job.aggiornato_il)}</span>
  </button>

  {#if attiva}
    <div class="job-stepper">
      <JobStepper fase={job.fase} />
    </div>
  {/if}

  {#if expanded}
    <div class="job-details">
      <dl>
        <div>
          <dt>Tentativi</dt>
          <dd class="mono">{job.tentativi ?? 0}</dd>
        </div>
        {#if job.prossima_esecuzione}
          <div>
            <dt>Prossima esecuzione</dt>
            <dd class="mono">{orario(job.prossima_esecuzione)}</dd>
          </div>
        {/if}
        <div>
          <dt>Avviato da</dt>
          <dd>{origine}</dd>
        </div>
        {#if job.errore}
          <div>
            <dt>Errore</dt>
            <dd class="errore">{job.errore}</dd>
          </div>
        {/if}
      </dl>
      {#if job.fase === 'failed'}
        <button class="btn-retry" onclick={() => onRetry?.(job)}
                aria-label={`Riprova ${titolo}`}>Riprova</button>
      {/if}
    </div>
  {/if}
</li>

<style>
  .job {
    background: var(--bg-secondary); border: 1px solid var(--border-color);
    border-radius: 10px; overflow: hidden;
  }
  .job-failed { border-color: var(--danger-border); }
  .job-main {
    width: 100%; display: flex; align-items: center; gap: 0.75rem;
    background: none; border: none; color: var(--text-primary);
    padding: 0.6rem 0.75rem; text-align: left; cursor: pointer;
    font-size: 0.9rem;
  }
  .job-main:hover { background: var(--bg-hover); }
  .job-main:focus-visible { outline: 2px solid var(--accent); outline-offset: -2px; }
  .job-poster {
    width: 40px; height: 56px; object-fit: cover; border-radius: 6px;
    flex: 0 0 auto; background: var(--poster-frame-bg, var(--bg-tertiary));
    border: 1px solid var(--border-color);
  }
  .job-poster-empty {
    display: grid; place-items: center;
    background: var(--accent-bg); color: var(--accent-light);
    font-size: 15px; font-weight: 700;
  }
  .job-title {
    flex: 1; min-width: 0; overflow: hidden; text-overflow: ellipsis;
    white-space: nowrap; font-size: 0.92rem; font-weight: 500;
  }
  .job-tempo { color: var(--text-muted); font-size: 12px; white-space: nowrap; }
  .job-stepper { padding: 0 0.8rem 0.55rem; }

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
  .btn-retry:focus-visible { outline: 2px solid var(--accent); outline-offset: 2px; }

  @media (max-width: 768px) {
    .job-tempo { display: none; }
  }
</style>
