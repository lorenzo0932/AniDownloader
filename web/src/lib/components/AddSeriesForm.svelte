<script>
  // Modale aggiunta/modifica serie. Lo stato del form e le azioni di salvataggio
  // restano nel parent (DashboardPage): qui solo presentazione + fetch-name.
  import { api, posterUrl } from '../api.js';

  let { show = false, form, editing = -1, series = [], onclose, onsave, ondelete, onpick, onerror } = $props();
  let posterError = $state(false);

  async function fetchName() {
    if (!form.url.trim()) return;
    try {
      const result = await api.series.fetchName(form.url);
      if (result.name) form.name = result.name;
    } catch (e) {
      if (onerror) onerror('Impossibile recuperare il nome: ' + e.message);
    }
  }
</script>

{#if show}
  <div class="modal-overlay" onclick={onclose} onkeydown={(e) => e.key === 'Escape' && onclose()} role="dialog" aria-modal="true" tabindex="-1">
    <div class="modal-panel form-modal" onclick={(e) => e.stopPropagation()} role="presentation">
      <div class="form-modal-body">
        <h3>{editing >= 0 ? 'Modifica: ' + form.name : 'Aggiungi Nuova Serie'}</h3>

        <div class="form-layout">
          <div class="form-poster-col">
            <div class="poster-frame">
              {#if editing >= 0}
                {@const editItem = series.find(item => item._file_index === editing)}
                {#if editItem}
                  <img src={posterUrl(editItem.path)} alt="poster" class="poster-preview"
                    onerror={() => posterError = true} />
                {/if}
              {:else}
                <div class="poster-placeholder">Nessun<br />Percorso</div>
              {/if}
            </div>
          </div>
          <div class="form-fields-col">
            <div class="field-group">
              <label class="field-label" for="f-service">Servizio di Download</label>
              <div class="radio-row">
                <label class="radio-label">
                  <input type="radio" bind:group={form.service} value="animeW_scraper" />
                  AnimeW Scraper
                </label>
                <label class="radio-label">
                  <input type="radio" bind:group={form.service} value="animeU_scraper" />
                  AnimeU Scraper
                </label>
              </div>
            </div>

            <div class="field-group">
              <label class="field-label" for="f-name">Nome:</label>
              <div class="input-with-btn">
                <input id="f-name" type="text" bind:value={form.name} placeholder="Nome della serie" required />
                <button type="button" class="btn-small-icon" title="Recupera il nome dalla pagina web" onclick={fetchName}>
                  <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" width="14" height="14"><polyline points="23 4 23 10 17 10"/><polyline points="1 20 1 14 7 14"/><path d="M3.51 9a9 9 0 0114.85-3.36L23 10M1 14l4.64 4.36A9 9 0 0020.49 15"/></svg>
                </button>
              </div>
            </div>

            <div class="field-group">
              <label class="field-label" for="f-path">Percorso Cartella:</label>
              <div class="input-with-btn">
                <input id="f-path" type="text" bind:value={form.path} placeholder="/path/to/series" required />
                <button type="button" class="btn-small-icon" onclick={onpick} title="Sfoglia directory">
                  <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" width="14" height="14"><path d="M22 19a2 2 0 01-2 2H4a2 2 0 01-2-2V5a2 2 0 012-2h5l2 3h9a2 2 0 012 2z"/></svg>
                </button>
              </div>
            </div>

            <div class="field-group">
              <label class="field-label" for="f-url">URL Pagina Serie:</label>
              <input id="f-url" type="text" bind:value={form.url} placeholder="https://www.animeworld.ac/play/..." required />
            </div>

            <div class="field-group">
              <label class="checkbox-inline">
                <input type="checkbox" bind:checked={form.continue} />
                Continua numerazione
              </label>
              <label class="checkbox-inline">
                <input type="checkbox" bind:checked={form.highPriority} />
                Alta Priorit&agrave;
              </label>
            </div>

            <div class="field-group">
              <label class="field-label" for="f-passed">Episodi Passati:</label>
              <input id="f-passed" type="number" min="0" max="9999" bind:value={form.passedEpisodes} class="num-input" />
            </div>
          </div>
        </div>

        {#if editing >= 0}
          <div class="form-footer">
            <button class="btn-danger" onclick={ondelete}>Elimina Serie</button>
            <div class="footer-right">
              <button class="btn-cancel" onclick={onclose}>Annulla</button>
              <button class="btn-primary" onclick={onsave}>Salva Modifiche</button>
            </div>
          </div>
        {:else}
          <div class="form-footer">
            <div></div>
            <div class="footer-right">
              <button class="btn-cancel" onclick={onclose}>Annulla</button>
              <button class="btn-primary" onclick={onsave}>Salva Modifiche</button>
            </div>
          </div>
        {/if}
      </div>
    </div>
  </div>
{/if}

<style>
  .modal-overlay {
    position: fixed; top: 0; left: 0; right: 0; bottom: 0;
    display: flex; align-items: center; justify-content: center;
    background: var(--overlay); z-index: 200;
    border: none; padding: 0; cursor: default;
    animation: fadeIn 0.2s ease;
  }
  .form-modal {
    animation: scaleIn 0.25s ease-out;
    background: var(--bg-secondary); border: 1px solid var(--border-color); border-radius: 16px;
    padding: 1.5rem; min-width: 520px; max-width: 90vw;
    max-height: 90vh; overflow-y: auto;
  }
  .form-modal-body h3 { font-size: 1.1rem; font-weight: 700; margin-bottom: 1rem; color: var(--accent-light); }

  .form-layout { display: flex; gap: 1.25rem; }
  .form-poster-col { flex-shrink: 0; }
  .poster-frame {
    width: 140px; aspect-ratio: 2 / 3; border: 1px solid var(--border-color); border-radius: 6px;
    background: var(--poster-frame-bg); display: flex; align-items: center; justify-content: center;
    overflow: hidden;
  }
  .poster-preview { width: 100%; height: 100%; object-fit: cover; transform: translateZ(0); }
  .poster-placeholder { color: var(--text-muted-more); font-size: 0.8rem; text-align: center; line-height: 1.5; }
  .form-fields-col { flex: 1; min-width: 0; display: flex; flex-direction: column; gap: 0.75rem; }

  .field-group { }
  .field-label { display: block; font-size: 0.8rem; color: var(--text-secondary); margin-bottom: 0.25rem; }
  .field-group input[type="text"], .field-group input[type="number"] {
    width: 100%; padding: 0.55rem 0.75rem; background: var(--bg-tertiary); border: 1px solid var(--border-color);
    border-radius: 6px; color: var(--text-primary); font-size: 0.85rem;
  }
  .field-group input:focus { outline: none; border-color: var(--accent); }
  .num-input { max-width: 100px; }
  .input-with-btn { display: flex; gap: 0.3rem; }
  .input-with-btn input { flex: 1; }
  .btn-small-icon {
    padding: 0.45rem 0.5rem; background: var(--bg-tertiary); border: 1px solid var(--border-color);
    border-radius: 6px; color: var(--text-muted); cursor: pointer; display: flex; align-items: center;
    flex-shrink: 0;
  }
  .btn-small-icon:hover { background: var(--bg-hover); color: var(--text-primary); }
  .btn-small-icon:disabled { cursor: not-allowed; opacity: 0.5; }

  .radio-row { display: flex; gap: 1rem; }
  .radio-label {
    display: flex; align-items: center; gap: 0.35rem;
    font-size: 0.85rem; color: var(--text-secondary); cursor: pointer;
  }
  .radio-label input[type="radio"] { accent-color: var(--accent); margin: 0; }
  .checkbox-inline {
    display: flex; align-items: center; gap: 0.35rem;
    font-size: 0.85rem; color: var(--text-secondary); cursor: pointer; margin-right: 1rem;
  }
  .checkbox-inline input[type="checkbox"] { accent-color: var(--accent); margin: 0; }

  .form-footer {
    display: flex; justify-content: space-between; align-items: center;
    padding-top: 1rem; margin-top: 0.5rem; border-top: 1px solid var(--border-color);
  }
  .footer-right { display: flex; gap: 0.5rem; align-items: center; }
  .btn-primary {
    padding: 0.55rem 1.1rem; background: var(--accent); border: none; border-radius: 8px;
    color: #fff; font-size: 0.85rem; font-weight: 600; cursor: pointer;
    display: flex; align-items: center; gap: 0.4rem;
  }
  .btn-primary:hover { background: var(--accent-hover); }
  .btn-cancel {
    padding: 0.55rem 1.1rem; background: none; border: 1px solid var(--btn-cancel-border); border-radius: 8px;
    color: var(--text-secondary); font-size: 0.85rem; cursor: pointer;
  }
  .btn-cancel:hover { background: var(--bg-tertiary); color: var(--text-primary); }
  .btn-danger {
    padding: 0.55rem 1.1rem; background: var(--danger); border: none; border-radius: 8px;
    color: #fff; font-size: 0.85rem; font-weight: 600; cursor: pointer;
    display: flex; align-items: center; gap: 0.4rem;
  }
  .btn-danger:hover { opacity: 0.85; }

  @keyframes fadeIn { from { opacity: 0; } to { opacity: 1; } }
  @keyframes scaleIn { from { opacity: 0; transform: scale(0.95); } to { opacity: 1; transform: scale(1); } }

  @media (max-width: 768px) {
    .form-modal {
      position: fixed; top: 0; left: 0; right: 0; bottom: 0;
      border-radius: 0; min-width: auto;
      max-width: 100vw; max-height: 100vh;
      padding-bottom: 96px;
    }
    .form-layout { flex-direction: column; }
    .form-poster-col { display: none; }
    .form-modal-body h3 { font-size: 1rem; }
    .btn-primary { padding: 0.45rem 0.85rem; font-size: 0.8rem; }
  }

  @media (orientation: landscape) and (max-height: 520px) {
    .form-modal {
      position: fixed; top: 0; left: 0; right: 0; bottom: 0;
      border-radius: 0; min-width: auto;
      max-width: 100vw; max-height: 100vh;
      padding-bottom: 48px;
    }
  }
</style>
