<script>
  import { onMount } from 'svelte';
  import { api } from '../api.js';
  import { getTheme, setTheme } from '../theme.svelte.js';

  let config = $state({});
  let error = $state('');
  let saved = $state(false);
  let busy = $state(true);
  let selectedTheme = $state(getTheme());

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
    num_chunks: 0,
    max_network_retries: 3,
    retry_delay_ms: 2000,
  };

  async function load() {
    busy = true;
    error = '';
    try {
      const data = await api.config.get();
      config = { ...DEFAULTS, ...(data.config || {}) };
    } catch (e) {
      error = e.message;
    } finally {
      busy = false;
    }
  }

  async function save() {
    error = '';
    saved = false;
    try {
      await api.config.set(config);
      saved = true;
      setTimeout(() => saved = false, 3000);
    } catch (e) {
      error = e.message;
    }
  }

  function handleThemeChange(t) {
    selectedTheme = t;
    setTheme(t);
  }

  onMount(load);
</script>

<div in:fly={{ y: 8, duration: 200 }}>
<h2>Impostazioni AniDownloader</h2>

{#if error}
  <div class="error">{error}</div>
{/if}

{#if busy}
  <div class="center"><div class="spinner-sm"></div></div>
{:else}
  <div class="form-card">
    <form onsubmit={(e) => { e.preventDefault(); save(); }}>

      <div class="group-box">
        <div class="group-title">Aspetto</div>

        <label for="theme-select">Tema:</label>
        <select id="theme-select" bind:value={selectedTheme} onchange={(e) => handleThemeChange(e.target.value)}>
          {#each THEME_OPTIONS as opt}
            <option value={opt.value}>{opt.label}</option>
          {/each}
        </select>
        <p class="field-hint">Scegli tra tema chiaro, scuro o automatico in base alle preferenze del browser.</p>
      </div>

      <div class="group-box">
        <div class="group-title">Gestione File e Percorsi</div>

        <label for="json_file_path">File Database Serie (JSON):</label>
        <div class="input-row">
          <input id="json_file_path" type="text" bind:value={config.json_file_path} class="readonly" readonly />
          <button type="button" class="btn-browse" disabled title="Percorso gestito dal server">...</button>
        </div>
        <p class="field-hint">Percorso del file di configurazione JSON delle serie</p>

        <label for="output_dir">Cartella di Destinazione (Output):</label>
        <div class="input-row">
          <input id="output_dir" type="text" bind:value={config.output_dir} placeholder="/path/to/downloads" />
          <button type="button" class="btn-browse" disabled title="Inserisci manualmente il percorso">...</button>
        </div>
        <p class="field-hint">Cartella di destinazione per i file scaricati</p>
      </div>

      <div class="group-box">
        <div class="group-title">Codifica e Prestazioni Video</div>

        <div class="checkbox-row">
          <input id="convert_to_h265" type="checkbox" bind:checked={config.convert_to_h265} />
          <label for="convert_to_h265">Abilita compressione H.265 (HEVC)</label>
        </div>
        <p class="field-hint">Riduce le dimensioni del file (~50%) mantenendo la qualit&agrave;.</p>

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

      <div class="form-footer">
        <button type="button" class="btn-cancel" onclick={() => load()}>Annulla</button>
        <button type="submit" class="btn-primary">Salva Modifiche</button>
        {#if saved}
          <span class="saved-msg">Salvato!</span>
        {/if}
      </div>
    </form>
  </div>
{/if}
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
    border-radius: 50%; animation: spin 0.8s linear infinite;
  }
  @keyframes spin { to { transform: rotate(360deg); } }
  .form-card {
    background: var(--bg-secondary); border: 1px solid var(--border-color); border-radius: 12px;
    padding: 1.5rem; max-width: 600px;
    animation: fadeIn 0.3s ease-out;
  }
  @keyframes fadeIn {
    from { opacity: 0; transform: translateY(6px); }
    to { opacity: 1; transform: translateY(0); }
  }
  .group-box {
    margin-bottom: 1.5rem; padding-bottom: 1rem;
    border-bottom: 1px solid var(--border-color);
  }
  .group-box:last-of-type { border-bottom: none; margin-bottom: 0; padding-bottom: 0; }
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
  select {
    width: 100%; padding: 0.65rem 0.9rem; background: var(--bg-tertiary); border: 1px solid var(--border-color);
    border-radius: 8px; color: var(--text-primary); font-size: 0.9rem; margin-bottom: 0.5rem;
    cursor: pointer;
  }
  select:focus { outline: none; border-color: var(--accent); }
  .input-row { display: flex; gap: 0.4rem; }
  .input-row input { flex: 1; margin-bottom: 0.25rem; }
  input[type="text"], input[type="number"] {
    width: 100%; padding: 0.65rem 0.9rem; background: var(--bg-tertiary); border: 1px solid var(--border-color);
    border-radius: 8px; color: var(--text-primary); font-size: 0.9rem; margin-bottom: 0.5rem;
  }
  input[type="text"]:focus, input[type="number"]:focus { outline: none; border-color: var(--accent); }
  input.readonly { color: var(--text-muted); cursor: not-allowed; }
  .btn-browse {
    padding: 0.65rem 0.8rem; background: var(--bg-tertiary); border: 1px solid var(--border-color);
    border-radius: 8px; color: var(--text-muted); cursor: not-allowed; font-size: 0.9rem;
    flex-shrink: 0;
  }
  .field-hint {
    font-size: 0.78rem; color: var(--text-muted); margin: -0.25rem 0 0.75rem; line-height: 1.4;
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
  .form-footer {
    display: flex; align-items: center; gap: 0.75rem;
    padding-top: 1rem; margin-top: 0.5rem;
    border-top: 1px solid var(--border-color); justify-content: flex-end;
  }
  .btn-primary {
    padding: 0.6rem 1.5rem; background: var(--accent); border: none; border-radius: 8px;
    color: #fff; font-size: 0.85rem; font-weight: 600; cursor: pointer;
  }
  .btn-primary:hover { background: var(--accent-hover); }
  .btn-cancel {
    padding: 0.6rem 1.25rem; background: none; border: 1px solid var(--border-color); border-radius: 8px;
    color: var(--text-secondary); font-size: 0.85rem; cursor: pointer;
  }
  .btn-cancel:hover { background: var(--bg-tertiary); color: var(--text-primary); }
  .saved-msg { color: var(--success); font-size: 0.85rem; }
</style>
