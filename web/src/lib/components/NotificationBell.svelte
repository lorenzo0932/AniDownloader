<script>
  import { api } from '../api.js';

  // Campanella "pronto da guardare": vive nella shell, una sola istanza.
  // - GET all'avvio per badge corretto anche dopo un riavvio del demone;
  // - SSE per aggiungere i nuovi episodi in tempo reale;
  // - notifica di sistema solo se la config lo prevede (sorgente backend).
  let { enabled = false, sorgente = 'entrambi' } = $props();

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

  function osNotify(item) {
    if (sorgente !== 'backend' && sorgente !== 'entrambi') return;
    const title = 'Nuovo episodio pronto';
    const body = `${item.serie} — Ep. ${item.episodio}`;
    // Desktop Tauri: comando Rust dedicato (plugin notification, nessun
    // pacchetto npm in più). Nel browser si usa la Web Notification API.
    const tauri = typeof window !== 'undefined' ? window.__TAURI_INTERNALS__ : null;
    if (tauri?.invoke) {
      tauri.invoke('notify_episode', { title, body }).catch(() => {});
      return;
    }
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

  // Reagisce anche al toggle in Impostazioni: abilitando dalle impostazioni
  // la campanella compare e apre l'SSE senza ricaricare la pagina; a OFF
  // chiude la connessione e non lascia badge orfani.
  $effect(() => {
    if (!enabled) return;
    load();
    let es = null;
    try {
      es = api.sse();
      es.onmessage = (event) => {
        try {
          const data = JSON.parse(event.data);
          if (data.type !== 'notification') return;
          items = [data, ...items].slice(0, MAX_ITEMS);
          unread += 1;
          osNotify(data);
        } catch {
          /* evento non JSON: ignora */
        }
      };
    } catch {
      es = null;
    }
    return () => {
      if (es) es.close();
    };
  });
</script>

{#if enabled}
  <div class="notif">
    {#if open}
      <div class="notif-panel" role="dialog" aria-label="Notifiche">
        <div class="notif-head">
          <strong>Notifiche</strong>
          <button class="notif-link" onclick={segnaTutte} disabled={unread === 0}>
            Segna tutte come lette
          </button>
        </div>
        {#if items.length === 0}
          <p class="notif-empty">Nessun episodio pronto.</p>
        {:else}
          <ul class="notif-list">
            {#each items as item (item.id)}
              <li>
                <button class="notif-item" class:unread={!item.letta}
                        onclick={() => segnaLetta(item)}
                        title={item.letta ? 'Letta' : 'Segna come letta'}>
                  <span class="notif-dot" class:on={!item.letta}></span>
                  <span class="notif-text">{item.serie} — Ep. {item.episodio}</span>
                </button>
              </li>
            {/each}
          </ul>
        {/if}
      </div>
    {/if}
    <button class="notif-bell" class:has-unread={unread > 0} onclick={() => (open = !open)}
            title="Notifiche" aria-label="Notifiche" aria-expanded={open}>
      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"
           width="18" height="18">
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
    width: 300px; max-height: 360px; overflow-y: auto;
    background: var(--bg-secondary); border: 1px solid var(--border-color);
    border-radius: 10px; box-shadow: 0 10px 30px rgba(0, 0, 0, 0.35);
  }
  .notif-head {
    display: flex; align-items: center; justify-content: space-between; gap: 0.5rem;
    padding: 0.7rem 0.8rem; border-bottom: 1px solid var(--border-color);
    position: sticky; top: 0; background: var(--bg-secondary);
  }
  .notif-link {
    background: none; border: none; color: var(--accent-light); cursor: pointer;
    font-size: 12px;
  }
  .notif-link:disabled { color: var(--text-muted); cursor: default; }
  .notif-empty { padding: 1rem; color: var(--text-muted); font-size: 13px; }
  .notif-list { list-style: none; margin: 0; padding: 0.3rem; }
  .notif-item {
    display: flex; align-items: center; gap: 0.5rem; width: 100%;
    padding: 0.5rem; border: none; border-radius: 6px; background: none;
    color: var(--text-secondary); cursor: pointer; text-align: left;
    font-size: 13px;
  }
  .notif-item:hover { background: var(--bg-hover); }
  .notif-item.unread { color: var(--text-primary); font-weight: 600; }
  .notif-dot {
    width: 7px; height: 7px; border-radius: 50%; flex: 0 0 auto;
    background: transparent; border: 1px solid var(--border-color);
  }
  .notif-dot.on { background: var(--accent); border-color: var(--accent); }
  .notif-text { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }

  @media (max-width: 720px) {
    .notif { margin-top: 0; }
    .notif-bell { width: auto; padding: 0.6rem; }
    .notif-panel { left: auto; right: 0; width: min(300px, 90vw); }
  }
</style>
