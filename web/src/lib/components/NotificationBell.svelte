<script>
  import { api } from '../api.js';
  import { contentEnter, panelExit } from '../motion.js';

  // Campanella "pronto da guardare": vive nella shell, una sola istanza.
  // - GET all'avvio per badge corretto anche dopo un riavvio del demone;
  // - SSE per aggiungere i nuovi episodi in tempo reale;
  // - le notifiche di sistema le invia il demone su Linux (osNotifyBackend);
  //   su macOS/Windows resta il client Tauri come fallback.
  let {
    enabled = false,
    sorgente = 'entrambi',
    osNotifyBackend = false,
  } = $props();

  let items = $state([]);
  let unread = $state(0);
  let open = $state(false);

  const MAX_ITEMS = 50;

  async function load() {
    if (!enabled) return;
    try {
      const data = await api.notifications.list();
      items = data.items || [];
      unread = data.unread || 0;
    } catch {
      /* backend non raggiungibile: la campanella resta vuota */
    }
  }

  async function segnaLetta(item) {
    if (!item || item.letta) return;
    try {
      const res = await api.notifications.read(item.id);
      items = items.map((i) => (i.id === item.id ? { ...i, letta: true } : i));
      unread = Number.isInteger(res.unread) ? res.unread : Math.max(0, unread - 1);
    } catch {
      /* nessun cambio locale: il badge resta coerente col server */
    }
  }

  async function segnaTutte() {
    try {
      const res = await api.notifications.read();
      items = items.map((i) => ({ ...i, letta: true }));
      unread = Number.isInteger(res.unread) ? res.unread : 0;
    } catch {
      /* idem */
    }
  }

  // Click su un episodio: segna letta, chiude il pannello e apre la serie in
  // Gestione. Non rimuove la notifica: lo storico resta consultabile.
  function apriNotifica(item) {
    if (!item) return;
    // Navigazione immediata: il POST "letta" aggiorna il badge in background.
    open = false;
    window.dispatchEvent(
      new CustomEvent('anidl:open-series', { detail: { serie: item.serie } })
    );
    void segnaLetta(item);
  }

  function webNotify(title, body) {
    if (typeof Notification === 'undefined') return;
    if (Notification.permission === 'granted') {
      new Notification(title, { body });
    } else if (Notification.permission === 'default') {
      Notification.requestPermission()
        .then((p) => {
          if (p === 'granted') new Notification(title, { body });
        })
        .catch(() => {});
    }
  }

  function osNotify(item, osBackend = null) {
    // Il demone ha già inviato (o ha tentato): niente doppioni.
    if (osBackend === true) return;
    // Backend Linux proprietario delle notifiche OS: se l'evento non dice
    // nulla (backend vecchio), evita comunque il doppione.
    if (osBackend === null && osNotifyBackend) return;
    if (sorgente !== 'backend' && sorgente !== 'entrambi') return;

    const title = 'Nuovo episodio pronto';
    const body = `${item.serie} — Ep. ${item.episodio}`;
    // Desktop Tauri: comando Rust dedicato (plugin notification, nessun
    // pacchetto npm in più). Nel browser si usa la Web Notification API.
    const tauri = typeof window !== 'undefined' ? window.__TAURI_INTERNALS__ : null;
    if (tauri?.invoke) {
      tauri.invoke('notify_episode', { title, body }).catch(() => webNotify(title, body));
      return;
    }
    webNotify(title, body);
  }

  let gruppi = $derived.by(() => {
    const mappa = new Map();
    const ordine = [];
    for (const item of items) {
      let gruppo = mappa.get(item.serie);
      if (!gruppo) {
        gruppo = { serie: item.serie, items: [], unread: 0, ultimo: item };
        mappa.set(item.serie, gruppo);
        ordine.push(gruppo);
      }
      gruppo.items.push(item);
      if (!item.letta) gruppo.unread += 1;
    }
    return ordine;
  });

  function iniziale(serie) {
    const s = String(serie || '').trim();
    return s ? s[0].toUpperCase() : '?';
  }

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

  // Reagisce anche al toggle in Impostazioni: abilitando dalle impostazioni
  // la campanella compare e apre l'SSE senza ricaricare la pagina; a OFF
  // chiude la connessione e non lascia badge orfani.
  $effect(() => {
    if (!enabled) return;
    let es = null;
    let annullato = false;
    // Prima la fotografia, poi il live: un evento che arriva durante la GET
    // non viene sovrascritto dalla risposta iniziale.
    (async () => {
      await load();
      if (annullato) return;
      try {
        es = api.sse();
        es.onmessage = (event) => {
          try {
            const data = JSON.parse(event.data);
            if (data.type !== 'notification') return;
            items = [data, ...items].slice(0, MAX_ITEMS);
            unread += 1;
            osNotify(data, typeof data.os === 'boolean' ? data.os : null);
          } catch {
            /* evento non JSON: ignora */
          }
        };
      } catch {
        es = null;
      }
    })();
    return () => {
      annullato = true;
      if (es) es.close();
    };
  });
</script>

<svelte:window
  onclick={(e) => {
    if (open && !(e.target instanceof Element && e.target.closest('.notif')))
      open = false;
  }}
  onkeydown={(e) => { if (e.key === 'Escape' && open) open = false; }}
/>

{#if enabled}
  <div class="notif">
    {#if open}
      <div class="notif-panel" role="dialog" aria-label="Notifiche"
           in:contentEnter out:panelExit>
        <div class="notif-head">
          <div class="notif-head-copy">
            <strong>Notifiche</strong>
            {#if unread > 0}<span class="notif-head-count">{unread} non lette</span>{/if}
          </div>
          <button class="notif-link" onclick={segnaTutte} disabled={unread === 0}>
            Segna tutte come lette
          </button>
        </div>

        {#if items.length === 0}
          <p class="notif-empty">Nessun episodio pronto.</p>
        {:else}
          <ul class="notif-list">
            {#each gruppi as gruppo (gruppo.serie)}
              <li class="notif-group">
                <div class="notif-group-head">
                  <span class="notif-avatar" aria-hidden="true">{iniziale(gruppo.serie)}</span>
                  <div class="notif-group-copy">
                    <span class="notif-serie">{gruppo.serie}</span>
                    <span class="notif-sub">
                      {gruppo.items.length}
                      {gruppo.items.length === 1 ? 'episodio' : 'episodi'}
                      {#if gruppo.ultimo?.creato_il}
                        · {tempoRelativo(gruppo.ultimo.creato_il)}
                      {/if}
                    </span>
                  </div>
                  {#if gruppo.unread > 0}
                    <span class="notif-new">{gruppo.unread} nuovi</span>
                  {/if}
                </div>

                <div class="notif-eps">
                  {#each gruppo.items as item (item.id)}
                    <button
                      type="button"
                      class="notif-ep"
                      class:unread={!item.letta}
                      aria-label={`Apri ${item.serie}, episodio ${item.episodio}`}
                      title="Apri la serie in Gestione"
                      onclick={() => apriNotifica(item)}
                    >
                      <span>Ep. {item.episodio}</span>
                      {#if !item.letta}
                        <span class="notif-dot on" aria-hidden="true"></span>
                      {/if}
                    </button>
                  {/each}
                </div>
              </li>
            {/each}
          </ul>
        {/if}
        <span class="sr-only" aria-live="polite">
          {unread} {unread === 1 ? 'notifica non letta' : 'notifiche non lette'}
        </span>
      </div>
    {/if}

    <button type="button" class="notif-bell" class:has-unread={unread > 0}
            onclick={() => (open = !open)}
            title="Notifiche" aria-label="Notifiche" aria-expanded={open}>
      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"
           width="18" height="18" aria-hidden="true">
        <path d="M18 8a6 6 0 00-12 0c0 7-3 9-3 9h18s-3-2-3-9"/>
        <path d="M13.73 21a2 2 0 01-3.46 0"/>
      </svg>
      {#if unread > 0}
        <span class="notif-badge">{unread > 99 ? '99+' : unread}</span>
      {/if}
    </button>
  </div>
{/if}

<style>
  .notif { position: relative; margin-top: 0.5rem; }
  .notif-bell {
    display: flex; align-items: center; justify-content: center; gap: 0.5rem;
    width: 100%; padding: 0.6rem; border: none; border-radius: 8px;
    background: none; color: var(--text-secondary); cursor: pointer;
  }
  .notif-bell:hover { background: var(--bg-hover); color: var(--text-primary); }
  .notif-bell.has-unread { color: var(--accent-light); }
  .notif-badge {
    min-width: 18px; height: 18px; padding: 0 5px; border-radius: 9px;
    background: var(--accent); color: #fff; font-size: 11px; font-weight: 700;
    line-height: 18px; text-align: center;
  }
  .notif-panel {
    position: absolute; left: 0; bottom: calc(100% + 8px); z-index: 60;
    width: 380px; max-height: 440px; overflow-y: auto;
    background: var(--bg-secondary); border: 1px solid var(--border-color);
    border-radius: 12px; box-shadow: 0 10px 30px rgba(0, 0, 0, 0.35);
  }
  .notif-head {
    display: flex; align-items: flex-start; justify-content: space-between; gap: 0.5rem;
    padding: 0.75rem 0.8rem; border-bottom: 1px solid var(--border-color);
    position: sticky; top: 0; background: var(--bg-secondary); z-index: 1;
  }
  .notif-head-copy { display: flex; flex-direction: column; gap: 0.15rem; }
  .notif-head-count { color: var(--text-muted); font-size: 11px; }
  .notif-link {
    background: none; border: none; color: var(--accent-light); cursor: pointer;
    font-size: 12px; padding: 0; white-space: nowrap;
  }
  .notif-link:disabled { color: var(--text-muted); cursor: default; }
  .notif-empty { padding: 1rem; color: var(--text-muted); font-size: 13px; }

  .notif-list { list-style: none; margin: 0; padding: 0.35rem; }
  .notif-group { padding: 0.35rem 0.25rem 0.5rem; }
  .notif-group + .notif-group { border-top: 1px solid var(--border-color); }
  .notif-group-head { display: flex; align-items: center; gap: 0.55rem; padding: 0.25rem 0.35rem; }
  .notif-avatar {
    width: 30px; height: 30px; flex: 0 0 auto; border-radius: 50%;
    display: grid; place-items: center;
    background: var(--accent-bg); color: var(--accent-light);
    font-size: 13px; font-weight: 700;
  }
  .notif-group-copy { min-width: 0; flex: 1; display: flex; flex-direction: column; gap: 0.1rem; }
  .notif-serie {
    font-size: 13px; font-weight: 600; color: var(--text-primary);
    white-space: nowrap; overflow: hidden; text-overflow: ellipsis;
  }
  .notif-sub { font-size: 11px; color: var(--text-muted); }
  .notif-new {
    font-size: 10px; font-weight: 700; white-space: nowrap;
    color: var(--accent-light); background: var(--accent-bg);
    border-radius: 999px; padding: 0.1rem 0.4rem;
  }
  .notif-eps { display: flex; flex-wrap: wrap; gap: 0.3rem; padding: 0.35rem 0.35rem 0 2.9rem; }
  .notif-ep {
    display: inline-flex; align-items: center; gap: 0.35rem;
    padding: 0.3rem 0.55rem; border-radius: 999px;
    background: var(--bg-tertiary); border: 1px solid var(--border-color);
    color: var(--text-secondary); cursor: pointer; font-size: 12px;
    font-family: inherit;
  }
  .notif-ep:hover { border-color: var(--accent); color: var(--text-primary); }
  .notif-ep.unread { color: var(--text-primary); font-weight: 600; border-color: var(--accent); }
  .notif-dot {
    width: 6px; height: 6px; border-radius: 50%; flex: 0 0 auto;
    background: transparent; border: 1px solid var(--border-color);
  }
  .notif-dot.on { background: var(--accent); border-color: var(--accent); }

  @media (max-width: 720px) {
    .notif { margin-top: 0; }
    .notif-bell { width: auto; padding: 0.6rem; }
    .notif-panel { left: auto; right: 0; width: min(380px, 92vw); }
  }
</style>
