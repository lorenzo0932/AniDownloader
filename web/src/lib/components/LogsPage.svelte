<script>
  import { onMount, tick } from 'svelte';
  import { pageEnter } from '../motion.js';
  import { api } from '../api.js';
  import QueuePage from './QueuePage.svelte';
  import LogRow from './LogRow.svelte';

  // Formato del logger: [2026-11-08 18:33:05] [INFO  ] messaggio
  const RE = /^\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})\]\s+\[([A-Z]+)\s*\]\s?(.*)$/;
  const MAX_RENDER = 1500;
  const POLL_MS = 2000;
  const RIGHE = [100, 500, 1000, 2000, 5000];
  const CHIPS = [
    ['tutti', 'Tutti'],
    ['error', 'Errori'],
    ['warn', 'Avvisi'],
    ['info', 'Info'],
    ['result', 'Risultati'],
  ];
  const LIVELLO_FILTRO = { ERROR: 'error', WARN: 'warn', INFO: 'info', RESULT: 'result' };

  let lines = $state([]);
  let count = $state(500);
  let tab = $state('log');
  let search = $state('');
  let livello = $state('tutti');
  let follow = $state(true);
  let wrap = $state(false);
  let busy = $state(true);
  let error = $state('');
  let nuovi = $state(0);
  let copiato = $state(false);

  let viewport = $state(null);
  let disposed = false;
  let copyTimer = null;

  function parseLines(rawLines) {
    const occorrenze = new Map();
    return rawLines.map((raw) => {
      const m = RE.exec(raw);
      const time = m ? m[1] : '';
      const level = m ? m[2] : 'ALTRO';
      const message = m ? m[3] : raw;
      const base = `${time}|${level}|${message}`;
      const n = occorrenze.get(base) || 0;
      occorrenze.set(base, n + 1);
      return { id: `${base}|${n}`, time, level, message, raw };
    });
  }

  function chiaveLivello(level) {
    return LIVELLO_FILTRO[level] || 'altro';
  }

  // Conteggi calcolati sulle righe che matchano la ricerca: i chip riflettono
  // quello che si vedrebbe selezionandoli.
  let ricercate = $derived.by(() => {
    const q = search.trim().toLowerCase();
    if (!q) return lines;
    return lines.filter(
      (e) =>
        e.message.toLowerCase().includes(q) ||
        e.level.toLowerCase().includes(q) ||
        e.time.toLowerCase().includes(q)
    );
  });

  let counts = $derived.by(() => {
    const c = { tutti: ricercate.length, error: 0, warn: 0, info: 0, result: 0 };
    for (const e of ricercate) {
      const k = chiaveLivello(e.level);
      if (k in c) c[k] += 1;
    }
    return c;
  });

  let filtrate = $derived.by(() => {
    if (livello === 'tutti') return ricercate;
    return ricercate.filter((e) => chiaveLivello(e.level) === livello);
  });

  let troncato = $derived(filtrate.length > MAX_RENDER);
  let visibili = $derived(troncato ? filtrate.slice(-MAX_RENDER) : filtrate);
  let filtriAttivi = $derived(search.trim() !== '' || livello !== 'tutti');
  let inCaricamento = $derived(busy && lines.length === 0);

  function stessoContenuto(a, b) {
    if (a.length !== b.length) return false;
    for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return false;
    return true;
  }

  // Numero di righe arrivate dopo l'ultima già vista: si cerca l'ultima riga
  // precedente nella nuova risposta (le ultime N righe possono scorrere).
  function contaNuovi(vecchie, nuove) {
    if (!nuove.length) return 0;
    if (!vecchie.length) return nuove.length;
    const ultima = vecchie[vecchie.length - 1];
    const idx = nuove.lastIndexOf(ultima);
    if (idx === -1) return nuove.length;
    return nuove.length - 1 - idx;
  }

  function inFondo() {
    if (!viewport) return true;
    return viewport.scrollHeight - viewport.scrollTop - viewport.clientHeight < 24;
  }

  async function vaiInFondo() {
    await tick();
    if (viewport) viewport.scrollTop = viewport.scrollHeight;
  }

  async function load({ silent = false } = {}) {
    if (!silent) busy = true;
    try {
      const data = await api.logs(count);
      if (disposed) return;
      const raw = data.lines || [];
      const precedenti = lines.map((e) => e.raw);
      if (!stessoContenuto(precedenti, raw)) {
        const arrivate = contaNuovi(precedenti, raw);
        lines = parseLines(raw);
        if (follow && inFondo()) {
          await vaiInFondo();
          nuovi = 0;
        } else {
          // Live tail in pausa o utente che sta leggendo più in alto: le
          // righe nuove si accumulano nella pill, senza rubare lo scroll.
          nuovi += arrivate;
        }
      }
      error = '';
    } catch (e) {
      if (!disposed) error = e.message || 'Errore nel caricamento dei log';
    } finally {
      if (!silent && !disposed) busy = false;
    }
  }

  // Polling solo quando serve: live tail attivo, tab Log e scheda visibile.
  $effect(() => {
    if (tab !== 'log' || !follow) return;
    const timer = setInterval(() => {
      if (document.hidden) return;
      load({ silent: true });
    }, POLL_MS);
    return () => clearInterval(timer);
  });

  onMount(() => {
    load();
    return () => {
      disposed = true;
      if (copyTimer) clearTimeout(copyTimer);
    };
  });

  function onScroll() {
    if (!viewport) return;
    // Tornare in fondo con lo scroll riallinea la live tail: la pill sparisce
    // e il prossimo aggiornamento torna a scorrere da solo.
    if (inFondo() && nuovi > 0) nuovi = 0;
  }

  async function tornaInFondo() {
    follow = true;
    nuovi = 0;
    await load({ silent: true });
    await vaiInFondo();
  }

  async function toggleSegui() {
    follow = !follow;
    if (follow) {
      nuovi = 0;
      await load({ silent: true });
      await vaiInFondo();
    }
  }

  // Aggiornamento manuale silenzioso: niente skeleton/lampeggio, le righe
  // nuove finiscono nella pill se l'utente è in pausa.
  function aggiorna() {
    load({ silent: true });
  }

  function cambiaCount(e) {
    const n = Number(e.currentTarget.value);
    if (!Number.isFinite(n) || n === count) return;
    count = n;
    nuovi = 0;
    load();
  }

  function cambiaTab(nuovo, focus = false) {
    if (tab === nuovo) return;
    tab = nuovo;
    if (nuovo === 'log') load({ silent: lines.length > 0 });
    if (focus) {
      tick().then(() => document.getElementById(`tab-${nuovo}`)?.focus());
    }
  }

  function gestisciTastiTab(e) {
    if (!['ArrowLeft', 'ArrowRight', 'Home', 'End'].includes(e.key)) return;
    e.preventDefault();
    let nuovo;
    if (e.key === 'Home') nuovo = 'log';
    else if (e.key === 'End') nuovo = 'diagnostica';
    else nuovo = tab === 'log' ? 'diagnostica' : 'log';
    cambiaTab(nuovo, true);
  }

  async function copia() {
    const testo = filtrate.map((e) => e.raw).join('\n');
    let ok = false;
    try {
      if (navigator.clipboard?.writeText) {
        await navigator.clipboard.writeText(testo);
        ok = true;
      }
    } catch {
      ok = false;
    }
    if (!ok) {
      try {
        const area = document.createElement('textarea');
        area.value = testo;
        area.setAttribute('readonly', '');
        area.style.position = 'fixed';
        area.style.opacity = '0';
        document.body.appendChild(area);
        area.select();
        ok = document.execCommand('copy');
        area.remove();
      } catch {
        ok = false;
      }
    }
    if (ok) {
      copiato = true;
      if (copyTimer) clearTimeout(copyTimer);
      copyTimer = setTimeout(() => {
        copiato = false;
      }, 2000);
    }
  }

  function scarica() {
    if (typeof URL.createObjectURL !== 'function') return;
    const testo = filtrate.map((e) => e.raw).join('\n');
    const url = URL.createObjectURL(new Blob([testo], { type: 'text/plain' }));
    const a = document.createElement('a');
    a.href = url;
    a.download = 'anidownloader-log.txt';
    document.body.appendChild(a);
    a.click();
    a.remove();
    URL.revokeObjectURL(url);
  }

  function azzeraFiltri() {
    search = '';
    livello = 'tutti';
  }
</script>

<div in:pageEnter|global class="logs-page">
  <h2>Log</h2>

  <div class="tabs" role="tablist" aria-label="Sezioni log">
    <button
      type="button"
      role="tab"
      id="tab-log"
      aria-selected={tab === 'log'}
      aria-controls="panel-log"
      tabindex={tab === 'log' ? 0 : -1}
      class:active={tab === 'log'}
      onclick={() => cambiaTab('log')}
      onkeydown={gestisciTastiTab}>Log</button
    >
    <button
      type="button"
      role="tab"
      id="tab-diagnostica"
      aria-selected={tab === 'diagnostica'}
      aria-controls="panel-diagnostica"
      tabindex={tab === 'diagnostica' ? 0 : -1}
      class:active={tab === 'diagnostica'}
      onclick={() => cambiaTab('diagnostica')}
      onkeydown={gestisciTastiTab}>Diagnostica</button
    >
  </div>

  {#if tab === 'log'}
    <div id="panel-log" role="tabpanel" aria-labelledby="tab-log" class="panel">
      <div class="toolbar">
        <input
          class="search"
          type="search"
          placeholder="Cerca nei log…"
          aria-label="Cerca nei log"
          bind:value={search}
        />
        <div class="chips" role="group" aria-label="Filtra per livello">
          {#each CHIPS as [valore, label] (valore)}
            <button
              type="button"
              class="chip"
              class:active={livello === valore}
              aria-pressed={livello === valore}
              onclick={() => (livello = valore)}
            >
              {label} <span class="chip-count">{counts[valore]}</span>
            </button>
          {/each}
        </div>
      </div>

      <div class="toolbar secondaria">
        <button
          type="button"
          class="btn-segui"
          class:attivo={follow}
          aria-pressed={follow}
          onclick={toggleSegui}
        >
          <span class="dot" class:on={follow} aria-hidden="true"></span> Segui
        </button>

        <select value={count} onchange={cambiaCount} aria-label="Righe da mostrare">
          {#each RIGHE as n (n)}
            <option value={n}>{n} righe</option>
          {/each}
        </select>

        <button type="button" class="btn-ghost" aria-pressed={wrap} onclick={() => (wrap = !wrap)}>
          A capo
        </button>
        <button type="button" class="btn-ghost" onclick={copia}>{copiato ? 'Copiato' : 'Copia'}</button>
        <button type="button" class="btn-ghost" onclick={scarica}>Scarica</button>
        <span class="sr-only" aria-live="polite">{copiato ? 'Log copiati' : ''}</span>
        <button type="button" class="btn-ghost" onclick={aggiorna} disabled={busy}>Aggiorna</button>
      </div>

      {#if error && lines.length === 0}
        <div class="stato errore" role="alert">
          <p>{error}</p>
          <button type="button" class="btn-primary" onclick={aggiorna}>Riprova</button>
        </div>
      {:else if inCaricamento}
        <div class="skeleton" aria-hidden="true">
          {#each Array(8) as _, i (i)}
            <div class="skeleton-row"></div>
          {/each}
        </div>
      {:else if visibili.length === 0}
        <div class="stato vuoto">
          {#if filtriAttivi}
            <p>Nessun log corrisponde ai filtri.</p>
            <button type="button" class="btn-ghost" onclick={azzeraFiltri}>Azzera filtri</button>
          {:else}
            <p>Nessun log disponibile.</p>
          {/if}
        </div>
      {:else}
        {#if error}
          <div class="alert-inline" role="alert">
            <span>{error}</span>
            <button type="button" class="btn-ghost" onclick={aggiorna}>Riprova</button>
          </div>
        {/if}
        {#if troncato}
          <p class="hint-troncato">
            Mostro le ultime {MAX_RENDER} righe filtrate; scarica per tutte.
          </p>
        {/if}
        <div class="log-wrap">
          <!-- svelte-ignore a11y_no_noninteractive_tabindex -->
          <div
            class="log-viewport"
            role="log"
            aria-label="Log del server"
            aria-live="off"
            tabindex="0"
            bind:this={viewport}
            onscroll={onScroll}
          >
            {#each visibili as entry (entry.id)}
              <LogRow {entry} query={search} {wrap} />
            {/each}
          </div>
          {#if nuovi > 0}
            <button type="button" class="pill-nuovi" onclick={tornaInFondo}>
              {nuovi} {nuovi === 1 ? 'nuovo' : 'nuovi'} · Torna in fondo
            </button>
          {/if}
        </div>
      {/if}
    </div>
  {:else}
    <div id="panel-diagnostica" role="tabpanel" aria-labelledby="tab-diagnostica" class="panel">
      <p class="hint">
        Dettaglio tecnico dei controlli periodici. Lo stato di download e conversioni vive in
        <b>Attività</b>.
      </p>
      <QueuePage readonly embedded />
    </div>
  {/if}
</div>

<style>
  .logs-page { animation: motion-fade var(--motion-enter) var(--motion-ease-out); }
  h2 { font-size: 1.5rem; font-weight: 700; margin-bottom: 1rem; }

  .tabs {
    display: flex;
    gap: 0.25rem;
    border-bottom: 1px solid var(--border-color);
    margin-bottom: 1rem;
  }
  .tabs button {
    background: none;
    border: none;
    border-bottom: 2px solid transparent;
    color: var(--text-muted);
    font-size: 0.9rem;
    font-weight: 600;
    font-family: inherit;
    padding: 0.65rem 0.9rem;
    min-height: 44px;
    cursor: pointer;
  }
  .tabs button:hover { color: var(--text-primary); }
  .tabs button.active { color: var(--text-primary); border-bottom-color: var(--accent); }

  .toolbar {
    display: flex;
    align-items: center;
    flex-wrap: wrap;
    gap: 0.5rem;
    margin-bottom: 0.75rem;
  }
  .toolbar.secondaria { gap: 0.4rem; }

  .search {
    flex: 1 1 240px;
    min-width: 180px;
    min-height: 44px;
    padding: 0 0.75rem;
    background: var(--bg-secondary);
    border: 1px solid var(--border-color);
    border-radius: 8px;
    color: var(--text-primary);
    font-size: 0.85rem;
    font-family: inherit;
  }
  .search:focus-visible { outline: 2px solid var(--accent); outline-offset: 1px; }

  .chips { display: flex; flex-wrap: wrap; gap: 0.35rem; }
  .chip {
    display: inline-flex;
    align-items: center;
    gap: 0.35rem;
    min-height: 44px;
    padding: 0 0.75rem;
    background: var(--bg-secondary);
    border: 1px solid var(--border-color);
    border-radius: 20px;
    color: var(--text-secondary);
    font-size: 0.8rem;
    font-weight: 600;
    font-family: inherit;
    cursor: pointer;
  }
  .chip:hover { border-color: var(--accent); }
  .chip.active {
    background: var(--accent-bg);
    border-color: var(--accent);
    color: var(--accent-light);
  }
  .chip-count {
    font-variant-numeric: tabular-nums;
    color: var(--text-muted);
    background: var(--bg-tertiary);
    border-radius: 10px;
    padding: 0 6px;
    font-size: 0.7rem;
  }
  .chip.active .chip-count { color: var(--accent-light); background: transparent; }

  .toolbar button,
  .toolbar select {
    min-height: 44px;
    font-family: inherit;
    font-size: 0.8rem;
    border-radius: 8px;
    cursor: pointer;
  }
  .toolbar select {
    padding: 0 0.6rem;
    background: var(--bg-secondary);
    border: 1px solid var(--border-color);
    color: var(--text-primary);
  }
  .toolbar select:focus-visible,
  .toolbar button:focus-visible {
    outline: 2px solid var(--accent);
    outline-offset: 1px;
  }

  .btn-primary {
    padding: 0 1rem;
    background: var(--accent);
    border: none;
    color: #fff;
    font-weight: 600;
  }
  .btn-primary:hover:not(:disabled) { background: var(--accent-hover); }
  .btn-primary:disabled { opacity: 0.6; cursor: default; }

  .btn-ghost {
    padding: 0 0.8rem;
    background: var(--bg-secondary);
    border: 1px solid var(--border-color);
    color: var(--text-secondary);
    font-weight: 500;
  }
  .btn-ghost:hover { border-color: var(--accent); color: var(--text-primary); }
  .btn-ghost:disabled { opacity: 0.6; cursor: default; }
  .btn-ghost[aria-pressed='true'] {
    background: var(--accent-bg);
    border-color: var(--accent);
    color: var(--accent-light);
  }

  .btn-segui {
    display: inline-flex;
    align-items: center;
    gap: 0.4rem;
    padding: 0 0.8rem;
    background: var(--bg-secondary);
    border: 1px solid var(--border-color);
    color: var(--text-secondary);
    font-weight: 600;
  }
  .btn-segui.attivo { border-color: var(--accent); color: var(--text-primary); }
  .dot {
    width: 8px;
    height: 8px;
    border-radius: 50%;
    background: var(--text-muted);
  }
  .dot.on {
    background: var(--success);
    box-shadow: 0 0 0 3px rgba(74, 222, 128, 0.15);
  }

  .stato {
    background: var(--bg-secondary);
    border: 1px solid var(--border-color);
    border-radius: 12px;
    padding: 2rem 1rem;
    text-align: center;
    color: var(--text-muted);
    font-size: 0.9rem;
  }
  .stato p { margin-bottom: 0.75rem; }
  .stato.errore {
    background: var(--danger-bg);
    border-color: var(--danger-border);
    color: var(--danger);
    text-align: left;
  }

  .alert-inline {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 0.75rem;
    background: var(--danger-bg);
    border: 1px solid var(--danger-border);
    color: var(--danger);
    border-radius: 8px;
    padding: 0.5rem 0.75rem;
    margin-bottom: 0.75rem;
    font-size: 0.82rem;
  }

  .skeleton {
    background: var(--bg-secondary);
    border: 1px solid var(--border-color);
    border-radius: 12px;
    padding: 0.75rem 0;
  }
  .skeleton-row {
    height: 14px;
    margin: 7px 0.75rem;
    border-radius: 4px;
    background: linear-gradient(
      90deg,
      var(--bg-tertiary) 25%,
      var(--bg-hover) 50%,
      var(--bg-tertiary) 75%
    );
    background-size: 200% 100%;
    animation: motion-shimmer 1.4s linear infinite;
  }

  .hint-troncato {
    color: var(--warning-text);
    background: var(--warning-bg);
    border: 1px solid var(--warning-border);
    border-radius: 8px;
    padding: 0.4rem 0.75rem;
    margin-bottom: 0.5rem;
    font-size: 0.78rem;
  }

  .log-wrap { position: relative; }
  .log-viewport {
    background: var(--log-bg);
    border: 1px solid var(--border-color);
    border-radius: 12px;
    padding: 0.5rem 0;
    max-height: 70vh;
    overflow: auto;
    overscroll-behavior: contain;
    animation: motion-fade var(--motion-enter) var(--motion-ease-out);
  }
  .log-viewport:focus-visible { outline: 2px solid var(--accent); outline-offset: 1px; }

  .pill-nuovi {
    position: absolute;
    left: 50%;
    bottom: 12px;
    transform: translateX(-50%);
    min-height: 40px;
    padding: 0 1rem;
    background: var(--accent);
    border: none;
    border-radius: 20px;
    color: #fff;
    font-size: 0.8rem;
    font-weight: 600;
    font-family: inherit;
    cursor: pointer;
    box-shadow: 0 4px 14px rgba(0, 0, 0, 0.35);
  }
  .pill-nuovi:hover { background: var(--accent-hover); }

  .hint {
    color: var(--text-muted);
    font-size: 0.8rem;
    margin-bottom: 0.75rem;
  }

  .sr-only {
    position: absolute;
    width: 1px;
    height: 1px;
    padding: 0;
    margin: -1px;
    overflow: hidden;
    clip: rect(0, 0, 0, 0);
    white-space: nowrap;
    border: 0;
  }

  @media (max-width: 720px) {
    .search { flex-basis: 100%; }
    .toolbar.secondaria { gap: 0.35rem; }
  }
</style>
