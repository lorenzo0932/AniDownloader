<script>
  import { onMount } from 'svelte';
  import { pageEnter } from '../motion.js';
  import { api, BASE } from '../api.js';
  import { getTheme, setTheme } from '../theme.svelte.js';
  import Dropdown from '../Dropdown.svelte';
  import DirectoryBrowser from './DirectoryBrowser.svelte';
  import StoragePanel from './StoragePanel.svelte';

  // Tab: le preferenze vivono in un solo form sempre montato (pannelli
  // nascosti via `hidden`). Il salvataggio è automatico con debounce: non
  // esiste più un pulsante Salva, quindi cambiare tab non perde nulla.
  // Sistema è manutenzione: fuori dal form, azioni immediate.
  const TABS = [
    { id: 'generali', label: 'Generali' },
    { id: 'download', label: 'Download' },
    { id: 'automazione', label: 'Automazione' },
    { id: 'sistema', label: 'Sistema' },
  ];

  let config = $state({});
  let error = $state('');
  let busy = $state(true);
  let showBrowser = $state(false);
  let selectedTheme = $state(getTheme());
  let tab = $state('generali');

  // Autosave: `lastSaved` è il JSON dell'ultimo salvataggio riuscito.
  // `saveState` guida la barra di stato (idle/dirty/saving/saved/error).
  let loaded = $state(false);
  let lastSaved = $state('');
  let saveState = $state('idle');
  let saveError = $state('');
  let saveInFlight = false;
  let savePending = false;
  let testNotifica = $state('idle');
  let testMessaggio = $state('');
  const SAVE_DEBOUNCE_MS = 800;

  let dirty = $derived(loaded && JSON.stringify(config) !== lastSaved);

  function snapshot() {
    return JSON.stringify(config);
  }

  async function flushSave() {
    if (!loaded) return;
    if (saveInFlight) {
      savePending = true;
      return;
    }
    const payload = snapshot();
    if (payload === lastSaved) return;
    saveInFlight = true;
    saveState = 'saving';
    saveError = '';
    try {
      await api.config.set(JSON.parse(payload));
      lastSaved = payload;
      saveState = 'saved';
      // La shell aggiorna subito il toggle notifiche senza ricaricare.
      window.dispatchEvent(new CustomEvent('anidl:config-saved', { detail: JSON.parse(payload) }));
    } catch (e) {
      saveState = 'error';
      saveError = e.message || 'Salvataggio non riuscito';
    } finally {
      saveInFlight = false;
      if (saveState !== 'error' && (savePending || snapshot() !== lastSaved)) {
        savePending = false;
        await flushSave();
        return;
      }
      savePending = false;
    }
  }

  // Ogni modifica riprogramma il debounce. Il cleanup dell'effetto cancella
  // il timer precedente: una raffica di input produce un solo PUT.
  $effect(() => {
    const payload = JSON.stringify(config);
    if (!loaded) return;
    if (payload === lastSaved) return;
    saveState = 'dirty';
    const timer = setTimeout(() => void flushSave(), SAVE_DEBOUNCE_MS);
    return () => clearTimeout(timer);
  });

  function ripristina() {
    if (!lastSaved) return;
    config = JSON.parse(lastSaved);
    saveState = 'idle';
    saveError = '';
  }

  async function provaNotifica() {
    if (testNotifica === 'loading') return;
    testNotifica = 'loading';
    testMessaggio = '';
    try {
      const esito = await api.notifications.test();
      testNotifica = 'ok';
      testMessaggio = esito?.method
        ? `Notifica inviata (${esito.method}).`
        : 'Notifica inviata.';
    } catch (e) {
      testNotifica = 'errore';
      testMessaggio = e.message || 'Invio non riuscito.';
    }
    setTimeout(() => {
      if (testNotifica !== 'loading') {
        testNotifica = 'idle';
        testMessaggio = '';
      }
    }, 4500);
  }

  const THEME_OPTIONS = [
    { value: 'system', label: 'Sistema (default)' },
    { value: 'light', label: 'Chiaro' },
    { value: 'dark', label: 'Scuro' },
  ];

  const DEFAULTS = {
    show_stop_warning: true,
    show_close_warning: true,
    auto_cleanup_on_close: true,
    convert_to_h265: true,
    burst_on_manual_start: true,
    num_chunks: 0,
    max_network_retries: 3,
    retry_delay_ms: 2000,
    scheduling: { abilitato: true, intervalloMinuti: 15 },
    notifiche: { abilitate: false, sorgente: 'entrambi' },
  };

  const SORGENTE_OPTIONS = [
    { value: 'ui', label: 'Solo app (campanella)' },
    { value: 'backend', label: 'Solo sistema (notifiche PC)' },
    { value: 'entrambi', label: 'App + sistema' },
  ];

  async function load() {
    busy = true;
    error = '';
    loaded = false;
    try {
      const data = await api.config.get();
      const backend = data.config || {};
      config = { ...DEFAULTS, ...backend };
      // Merge profondo delle sezioni note: il backend potrebbe non averle
      // (config vecchia) senza che la UI perda i suoi default.
      config.scheduling = { ...DEFAULTS.scheduling, ...(backend.scheduling || {}) };
      config.notifiche = { ...DEFAULTS.notifiche, ...(backend.notifiche || {}) };
      // La baseline va presa DOPO i merge: altrimenti il primo render
      // sembrerebbe dirty e farebbe un PUT inutile.
      lastSaved = JSON.stringify(config);
      saveState = 'idle';
      saveError = '';
      loaded = true;
    } catch (e) {
      error = e.message;
    } finally {
      busy = false;
    }
  }

  function handleThemeChange(t) {
    selectedTheme = t;
    setTheme(t);
  }

  function pickJsonPath() {
    showBrowser = true;
  }

  function onBrowserSelect(selectedPath) {
    config.json_file_path = selectedPath;
    showBrowser = false;
  }

  function onBrowserCancel() {
    showBrowser = false;
  }

  // Roving tabindex: frecce sinistra/destra (più Home/End) spostano il tab
  // attivo e il focus, come da pattern ARIA per le tablist.
  function onTabKeydown(e, index) {
    const gestiti = ['ArrowLeft', 'ArrowRight', 'Home', 'End'];
    if (!gestiti.includes(e.key)) return;
    e.preventDefault();
    let next = index;
    if (e.key === 'ArrowLeft') next = (index - 1 + TABS.length) % TABS.length;
    else if (e.key === 'ArrowRight') next = (index + 1) % TABS.length;
    else if (e.key === 'Home') next = 0;
    else next = TABS.length - 1;
    tab = TABS[next].id;
    document.getElementById(`settings-tab-${TABS[next].id}`)?.focus();
  }

  onMount(load);
</script>

<div in:pageEnter|global>
  <div class="settings-shell">
    <h2>Impostazioni AniDownloader</h2>

    {#if error}
      <div class="error">{error}</div>
    {/if}

    {#if busy}
      <div class="center"><div class="spinner-sm"></div></div>
    {:else if config.notifiche && config.scheduling}
    <div class="tabs" role="tablist" aria-label="Sezioni impostazioni">
      {#each TABS as t, i (t.id)}
        <button type="button" role="tab"
                id={`settings-tab-${t.id}`}
                aria-selected={tab === t.id}
                aria-controls={`settings-panel-${t.id}`}
                tabindex={tab === t.id ? 0 : -1}
                class:active={tab === t.id}
                onclick={() => (tab = t.id)}
                onkeydown={(e) => onTabKeydown(e, i)}>
          {t.label}
        </button>
      {/each}
    </div>

    <div class="form-card">
      <form aria-label="Preferenze" hidden={tab === 'sistema'}
            onsubmit={(e) => e.preventDefault()}>
        <DirectoryBrowser
          show={showBrowser}
          currentPath={config.json_file_path || '~/Video'}
          title="Scegli file JSON"
          onselect={onBrowserSelect}
          oncancel={onBrowserCancel}
        />

        <div id="settings-panel-generali" class="panel" role="tabpanel"
             aria-labelledby="settings-tab-generali" hidden={tab !== 'generali'}>
          <div class="group-box">
            <div class="group-title">Aspetto</div>
            <label for="theme-select">Tema:</label>
            <Dropdown bind:value={selectedTheme} options={THEME_OPTIONS}
                      onchange={handleThemeChange} label="Tema" id="theme-select" />
            <p class="field-hint">Scegli tra tema chiaro, scuro o automatico in base alle preferenze del browser.</p>
            <p class="field-note">Il tema si applica subito; tutte le preferenze si salvano automaticamente.</p>
          </div>

          <div class="group-box">
            <div class="group-title">Comportamento e Avvisi</div>

            <div class="checkbox-row">
              <input id="show_stop_warning" type="checkbox" bind:checked={config.show_stop_warning} />
              <label for="show_stop_warning">Mostra conferma prima di interrompere un download</label>
            </div>

            <div class="checkbox-row">
              <input id="show_close_warning" type="checkbox" bind:checked={config.show_close_warning} />
              <label for="show_close_warning">Mostra conferma prima di chiudere l'app (durante un download)</label>
            </div>

            <hr class="field-sep" />

            <div class="checkbox-row">
              <input id="auto_cleanup_on_close" type="checkbox" bind:checked={config.auto_cleanup_on_close} />
              <label for="auto_cleanup_on_close">Pulizia Automatica dei file parziali</label>
            </div>
            <p class="field-hint">Rimuove automaticamente i file temporanei (.aria2) e i segmenti video in caso di interruzione o chiusura forzata.</p>

            <hr class="field-sep" />

            <label for="max_network_retries">Tentativi di rete massimi</label>
            <input id="max_network_retries" type="number" min="1" max="20" bind:value={config.max_network_retries} />

            <label for="retry_delay_ms">Ritardo tra tentativi (ms)</label>
            <input id="retry_delay_ms" type="number" min="100" max="30000" step="100" bind:value={config.retry_delay_ms} />
          </div>
        </div>

        <div id="settings-panel-download" class="panel" role="tabpanel"
             aria-labelledby="settings-tab-download" hidden={tab !== 'download'}>
          <div class="download-grid">
            <div class="group-box">
              <div class="group-title">Gestione File e Percorsi</div>

              <label for="json_file_path">File Database Serie (JSON):</label>
              <div class="input-row">
                <input id="json_file_path" type="text" bind:value={config.json_file_path} placeholder="/path/to/series.json" />
                <button type="button" class="btn-browse" onclick={pickJsonPath} title="Sfoglia">...</button>
              </div>
              <p class="field-hint">Percorso del file di configurazione JSON delle serie</p>
            </div>

            <div class="group-box">
              <div class="group-title">Codifica e Prestazioni Video</div>

              <div class="checkbox-row">
                <input id="convert_to_h265" type="checkbox" bind:checked={config.convert_to_h265} />
                <label for="convert_to_h265">Abilita compressione H.265 (HEVC)</label>
              </div>
              <p class="field-hint">Riduce le dimensioni del file (~50%) mantenendo la qualit&agrave;.</p>

              <hr class="field-sep" />

              <div class="checkbox-row">
                <input id="burst_on_manual_start" type="checkbox" bind:checked={config.burst_on_manual_start} />
                <label for="burst_on_manual_start">Download manuali con priorit&agrave; massima (burst)</label>
              </div>
              <p class="field-hint">
                Usa ~85% della CPU invece del 50% dei controlli automatici: pi&ugrave; veloce quando sei al PC,
                con pi&ugrave; calore e consumo. Vale dal prossimo avvio; lo scheduler resta sempre contenuto.
              </p>

              <hr class="field-sep" />

              <div class="sub-title">Elaborazione Parallela (Chunk Splitting)</div>
              <p class="field-hint">Divide il video in segmenti per accelerare la codifica su CPU multi-core.</p>

              <label for="num_chunks">Numero di Chunk:</label>
              <input id="num_chunks" type="number" min="0" max="32" bind:value={config.num_chunks}
                placeholder="0 = Auto" />
              <p class="field-hint">Numero di segmenti per la codifica parallela (0 = Auto)</p>

              <div class="warning-box">
                <svg class="warn-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
                  <path d="M10.29 3.86L1.82 18a2 2 0 001.71 3h16.94a2 2 0 001.71-3L13.71 3.86a2 2 0 00-3.42 0z"/>
                  <line x1="12" y1="9" x2="12" y2="13"/><line x1="12" y1="17" x2="12.01" y2="17"/>
                </svg>
                <div>
                  <b>Attenzione alle Prestazioni:</b><br />
                  &bull; <b>H.265:</b> Richiede molta potenza di calcolo.<br />
                  &bull; <b>Chunk &gt; 1:</b> Aumenta drasticamente l'uso di RAM e CPU.<br />
                  &bull; <b>Auto:</b> Adatta automaticamente le risorse in base al tuo PC.
                </div>
              </div>
            </div>
          </div>
        </div>

        <div id="settings-panel-automazione" class="panel" role="tabpanel"
             aria-labelledby="settings-tab-automazione" hidden={tab !== 'automazione'}>
          <div class="group-box">
            <div class="group-title">Controlli Automatici</div>

            <div class="checkbox-row">
              <input id="sched_abilitato" type="checkbox" bind:checked={config.scheduling.abilitato} />
              <label for="sched_abilitato">Controllo periodico nuovi episodi</label>
            </div>
            <p class="field-hint">Sostituisce il vecchio timer di sistema: il demone controlla da solo.</p>

            <label for="sched_intervallo">Ogni quanti minuti</label>
            <input id="sched_intervallo" type="number" min="5" max="1440" step="5"
              bind:value={config.scheduling.intervalloMinuti} disabled={!config.scheduling.abilitato} />
            <p class="field-hint">Da 5 minuti a 24 ore. La modifica vale dal prossimo giro, quello in corso non si interrompe.</p>
          </div>

          <div class="group-box">
            <div class="group-title">Notifiche</div>

            <div class="checkbox-row">
              <input id="notifiche_abilitate" type="checkbox" bind:checked={config.notifiche.abilitate} />
              <label for="notifiche_abilitate">Notifiche nuovi episodi</label>
            </div>

            <label for="notifiche_sorgente">Mostra tramite</label>
            <Dropdown bind:value={config.notifiche.sorgente} options={SORGENTE_OPTIONS}
                      onchange={() => {}} label="Mostra tramite" id="notifiche-sorgente" />
            <p class="field-hint">
              “Sistema” invia la notifica del PC anche con l'app chiusa, se il demone è in esecuzione.
              La campanella in-app resta sempre disponibile come storico.
            </p>

            <button type="button" class="btn-test" onclick={provaNotifica}
                    disabled={testNotifica === 'loading'}>
              {testNotifica === 'loading' ? 'Invio…' : 'Invia notifica di prova'}
            </button>
            {#if testMessaggio}
              <p class="test-esito" class:ok={testNotifica === 'ok'}
                 class:errore={testNotifica === 'errore'} role="status">
                {testMessaggio}
              </p>
            {/if}
          </div>
        </div>

        {#if tab !== 'sistema'}
          <div class="autosave-bar" aria-live="polite">
            <span class="autosave-state" class:errore={saveState === 'error'}>
              {#if saveState === 'saving'}
                <span class="save-dot saving" aria-hidden="true"></span> Salvataggio…
              {:else if saveState === 'error'}
                <span class="save-dot error" aria-hidden="true"></span> {saveError}
              {:else if dirty}
                <span class="save-dot pending" aria-hidden="true"></span> Modifiche in attesa…
              {:else if saveState === 'saved'}
                <span class="save-check" aria-hidden="true">✓</span> Salvato
              {:else}
                <span class="save-check muted" aria-hidden="true">✓</span> Tutto salvato
              {/if}
            </span>
            {#if dirty || saveState === 'error'}
              <button type="button" class="btn-cancel" onclick={ripristina}
                      disabled={saveState === 'saving'}>Ripristina</button>
            {/if}
            {#if saveState === 'error'}
              <button type="button" class="btn-primary" onclick={() => void flushSave()}>Riprova</button>
            {/if}
          </div>
        {/if}
      </form>

      <div id="settings-panel-sistema" class="panel" role="tabpanel"
           aria-labelledby="settings-tab-sistema" hidden={tab !== 'sistema'}>
        {#if tab === 'sistema'}
          <StoragePanel />
        {/if}
      </div>
    </div>
    {/if}
  </div>
</div>

<style>
  h2 { font-size: 1.5rem; font-weight: 700; margin-bottom: 1.5rem; }
  .error {
    background: var(--danger-bg); border: 1px solid var(--danger-border); color: var(--danger);
    padding: 0.75rem; border-radius: 8px; margin-bottom: 1rem; font-size: 0.85rem;
  }
  .center { display: flex; justify-content: center; padding: 2rem; }
  .spinner-sm {
    width: 24px; height: 24px; border: 2px solid var(--border-color); border-top-color: var(--accent);
    border-radius: 50%; animation: motion-spin var(--motion-spinner) linear infinite;
  }
  .settings-shell { max-width: 1100px; margin: 0 auto; }

  .tabs {
    display: flex; flex-wrap: wrap; gap: 0.25rem; margin-bottom: 1rem;
    border-bottom: 1px solid var(--border-color);
  }
  .tabs button {
    background: none; border: none; border-bottom: 2px solid transparent;
    color: var(--text-secondary); padding: 0.5rem 0.9rem; cursor: pointer;
    font-size: 0.9rem; font-family: inherit; border-radius: 6px 6px 0 0;
  }
  .tabs button:hover { background: var(--bg-tertiary); color: var(--text-primary); }
  .tabs button.active { color: var(--accent-light); border-bottom-color: var(--accent); }
  .tabs button:focus-visible { outline: 2px solid var(--accent); outline-offset: 2px; }

  .form-card {
    background: var(--bg-secondary); border: 1px solid var(--border-color); border-radius: 12px;
    padding: 1.5rem; margin: 0;
    animation: motion-fade var(--motion-enter) var(--motion-ease-out);
  }
  form[hidden] { display: none; }
  .panel[hidden] { display: none; }

  .group-box { margin-bottom: 1.5rem; }
  .group-box:last-child { margin-bottom: 0; }
  .group-title {
    font-size: 0.9rem; font-weight: 700; color: var(--accent-light);
    margin-bottom: 1rem; padding-bottom: 0.5rem;
    border-bottom: 1px solid var(--border-color);
  }
  .sub-title {
    font-size: 0.85rem; font-weight: 600; color: var(--text-secondary);
    margin-bottom: 0.25rem;
  }
  label { display: block; margin-bottom: 0.35rem; font-size: 0.85rem; color: var(--text-secondary); }

  .input-row { display: flex; gap: 0.4rem; }
  .input-row input { flex: 1; margin-bottom: 0.25rem; }
  input[type="text"], input[type="number"] {
    width: 100%; padding: 0.65rem 0.9rem; background: var(--bg-tertiary); border: 1px solid var(--border-color);
    border-radius: 8px; color: var(--text-primary); font-size: 0.9rem; margin-bottom: 0.5rem;
  }
  input[type="text"]:focus, input[type="number"]:focus { outline: none; border-color: var(--accent); }
  .btn-browse {
    padding: 0.65rem 0.8rem; background: var(--bg-tertiary); border: 1px solid var(--border-color);
    border-radius: 8px; color: var(--text-secondary); cursor: pointer; font-size: 0.9rem;
    flex-shrink: 0;
  }
  .btn-browse:hover { border-color: var(--accent); color: var(--accent); }
  .field-hint {
    font-size: 0.78rem; color: var(--text-muted); margin: 0 0 0.75rem; line-height: 1.4;
  }
  .field-note {
    font-size: 0.78rem; color: var(--text-secondary); line-height: 1.4;
    background: var(--bg-tertiary); border-left: 3px solid var(--accent);
    border-radius: 6px; padding: 0.5rem 0.65rem; margin: 0 0 0.25rem;
  }
  .field-sep {
    border: none; border-top: 1px solid var(--border-color); margin: 0.75rem 0;
  }
  .checkbox-row { display: flex; align-items: center; gap: 0.5rem; margin-bottom: 0.5rem; }
  .checkbox-row label { margin-bottom: 0; cursor: pointer; font-size: 0.85rem; color: var(--text-secondary); }
  input[type="checkbox"] { accent-color: var(--accent); width: 16px; height: 16px; cursor: pointer; flex-shrink: 0; }
  .warning-box {
    display: flex; gap: 0.6rem; background: var(--warning-bg); border: 1px solid var(--warning-border);
    border-radius: 8px; padding: 0.75rem; margin: 0.75rem 0; font-size: 0.78rem; color: var(--warning-text); line-height: 1.5;
  }
  .warn-icon { width: 20px; height: 20px; flex-shrink: 0; margin-top: 2px; color: var(--warning-text); }
  .autosave-bar {
    display: flex; align-items: center; flex-wrap: wrap; gap: 0.75rem;
    padding-top: 1.25rem; margin-top: 0.5rem;
    border-top: 1px solid var(--border-color);
    justify-content: flex-end;
    position: sticky; bottom: 0;
    background: var(--bg-secondary);
    padding-bottom: 0.25rem;
  }
  .autosave-state {
    margin-right: auto; display: inline-flex; align-items: center; gap: 0.4rem;
    font-size: 0.8rem; color: var(--text-muted);
  }
  .autosave-state.errore { color: var(--danger); }
  .save-dot {
    width: 8px; height: 8px; border-radius: 50%; flex-shrink: 0;
    background: var(--warning);
  }
  .save-dot.saving { animation: motion-pulse 1s ease-in-out infinite; }
  .save-dot.error { background: var(--danger); }
  .save-dot.pending { background: var(--warning); }
  .save-check { color: var(--success); font-weight: 700; }
  .save-check.muted { color: var(--text-muted); }
  .btn-primary {
    padding: 0.6rem 1.5rem; background: var(--accent); border: none; border-radius: 8px;
    color: #fff; font-size: 0.85rem; font-weight: 600; cursor: pointer;
  }
  .btn-primary:hover { background: var(--accent-hover); }
  .btn-cancel {
    padding: 0.6rem 1.25rem; background: none; border: 1px solid var(--border-color); border-radius: 8px;
    color: var(--text-secondary); font-size: 0.85rem; cursor: pointer;
  }
  .btn-cancel:hover:not(:disabled) { background: var(--bg-tertiary); color: var(--text-primary); }
  .btn-cancel:disabled { opacity: 0.5; cursor: default; }
  .btn-test {
    margin-top: 0.25rem; padding: 0.5rem 0.8rem;
    background: var(--bg-tertiary); border: 1px solid var(--border-color);
    border-radius: 8px; color: var(--text-secondary); font-size: 0.8rem;
    cursor: pointer; font-family: inherit;
  }
  .btn-test:hover:not(:disabled) { border-color: var(--accent); color: var(--text-primary); }
  .btn-test:disabled { opacity: 0.6; cursor: wait; }
  .test-esito { margin: 0.5rem 0 0; font-size: 0.78rem; color: var(--text-muted); }
  .test-esito.ok { color: var(--success); }
  .test-esito.errore { color: var(--danger); }
  /* Solo il tab Download usa due colonne su schermi larghi. */
  .download-grid { display: grid; grid-template-columns: 1fr; gap: 1.5rem; }
  @media (min-width: 700px) {
    .download-grid { grid-template-columns: repeat(auto-fit, minmax(320px, 1fr)); }
  }

  /* Ultrawide: Generali e Automazione sfruttano la larghezza senza
     allungare le righe di testo oltre misura. */
  @media (min-width: 1200px) {
    #settings-panel-generali:not([hidden]),
    #settings-panel-automazione:not([hidden]) {
      display: grid;
      grid-template-columns: repeat(2, minmax(0, 1fr));
      gap: 1.5rem;
      align-items: start;
    }
    #settings-panel-generali .group-box,
    #settings-panel-automazione .group-box { margin-bottom: 0; }
  }

  @media (max-width: 768px) {
    .form-card { padding: 1rem; }
    .autosave-bar { position: static; justify-content: flex-start; }
    .autosave-state { flex-basis: 100%; }
  }
</style>
