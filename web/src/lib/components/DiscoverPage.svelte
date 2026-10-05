<script>
  // Pagina Scopri: ricerca e filtri sul catalogo AnimeWorld (come la pagina
  // /filter del sito), griglia card con poster diretto, dettaglio lazy via
  // tooltip, Aggiungi con form precompilato. Paginazione a chunk da ~40
  // (una pagina del sito) con "carica altri": niente scarichi massivi.
  import { onMount } from 'svelte';
  import { api } from '../api.js';
  import AddSeriesForm from './AddSeriesForm.svelte';
  import DirectoryBrowser from './DirectoryBrowser.svelte';

  const ANIMEWORLD_BASE = 'https://www.animeworld.ac';

  const GENERI = [
    [1, 'Azione'], [2, 'Avventura'], [3, 'Arti Marziali'], [4, 'Commedia'],
    [5, 'Avanguardia'], [6, 'Demoni'], [7, 'Drammatico'], [8, 'Ecchi'],
    [9, 'Fantasy'], [10, 'Gioco'], [11, 'Harem'], [12, 'Storico'],
    [13, 'Horror'], [14, 'Josei'], [16, 'Magia'], [18, 'Mecha'],
    [19, 'Militari'], [20, 'Musicale'], [21, 'Mistero'], [22, 'Parodia'],
    [23, 'Polizia'], [24, 'Psicologico'], [25, 'Sentimentale'], [26, 'Samurai'],
    [27, 'Scolastico'], [28, 'Sci-Fi'], [29, 'Seinen'], [30, 'Shoujo'],
    [31, 'Shoujo Ai'], [32, 'Shounen'], [33, 'Shounen Ai'], [34, 'Slice of Life'],
    [35, 'Spazio'], [36, 'Sport'], [37, 'Soprannaturale'], [38, 'Superpoteri'],
    [39, 'Thriller'], [40, 'Vampiri'], [41, 'Yaoi'], [42, 'Yuri'],
    [43, 'Hentai'], [46, 'Romantico'], [47, 'Bambini'], [48, 'Veicoli'],
    [49, 'Isekai'],
  ];
  const STAGIONI = [['winter', 'Inverno'], ['spring', 'Primavera'], ['summer', 'Estate'], ['fall', 'Autunno'], ['unknown', 'Sconosciuta']];
  const TIPI = [[0, 'Anime'], [4, 'Movie'], [1, 'OVA'], [2, 'ONA'], [3, 'Special'], [5, 'Music']];
  const STATI = [[0, 'In corso'], [1, 'Finito'], [2, 'Non rilasciato'], [3, 'Droppato']];
  const LINGUE = [['jp', 'Giapponese'], ['it', 'Italiano'], ['ch', 'Cinese'], ['kr', 'Coreano'], ['en', 'Inglese'], ['fr', 'Francese']];
  const ORDINI = [[0, 'Standard'], [1, 'Ultime aggiunte'], [2, 'Lista A-Z'], [3, 'Lista Z-A'], [4, 'Più vecchi'], [5, 'Più recenti'], [6, 'Più visti'], [7, 'Meglio valutati']];
  const ANNI = [];
  for (let y = new Date().getFullYear() + 1; y >= 1966; y--) ANNI.push(y);

  let keyword = $state('');
  let generiSel = $state([]);
  let genreModeAnd = $state(false);
  let stagione = $state('');
  let anno = $state('');
  let tipo = $state('');
  let stato = $state('');
  let studio = $state('');
  let lingua = $state('');
  let soloDoppiati = $state(false);
  let ordine = $state(1);

  let items = $state([]);
  let page = $state(0);
  let maxPage = $state(1);
  let loading = $state(false);
  let loadingMore = $state(false);
  let error = $state('');
  let searched = $state(false);

  let detail = $state(null);
  let detailData = $state(null);
  let detailLoading = $state(false);

  let showForm = $state(false);
  let showBrowser = $state(false);
  let form = $state({ service: 'animeW_scraper', name: '', path: '', url: '', continue: false, highPriority: false, passedEpisodes: 0 });
  let notice = $state('');

  // Lock dello scroll di sfondo finche un modale e aperto
  // (dettaglio / form / browser): ripristino in cleanup.
  const anyModalOpen = $derived(detail !== null || showForm || showBrowser);
  $effect(() => {
    if (!anyModalOpen) return;
    const prev = document.body.style.overflow;
    document.body.style.overflow = 'hidden';
    return () => { document.body.style.overflow = prev; };
  });

  function buildOpts(p) {
    const opts = { page: p, sort: ordine };
    if (keyword.trim()) opts.keyword = keyword.trim();
    else {
      if (generiSel.length) opts.genre = [...generiSel];
      if (generiSel.length > 1 && genreModeAnd) opts.genre_mode = 'and';
      if (stagione) opts.season = stagione;
      if (anno) opts.year = anno;
      if (tipo !== '') opts.type = tipo;
      if (stato !== '') opts.status = stato;
      if (studio.trim()) opts.studio = studio.trim();
      if (lingua) opts.language = lingua;
      if (soloDoppiati) opts.dub = 1;
    }
    return opts;
  }

  async function cerca() {
    loading = true;
    error = '';
    notice = '';
    try {
      const data = await api.discover.page(buildOpts(1));
      items = data.items || [];
      page = data.page || 1;
      maxPage = data.maxPage || 1;
      searched = true;
    } catch (e) {
      error = 'Ricerca fallita: ' + e.message;
    } finally {
      loading = false;
    }
  }

  async function caricaAltri() {
    if (loadingMore || page >= maxPage) return;
    loadingMore = true;
    try {
      const data = await api.discover.page(buildOpts(page + 1));
      items = [...items, ...(data.items || [])];
      page = data.page || page + 1;
      maxPage = data.maxPage || maxPage;
    } catch (e) {
      error = 'Caricamento fallito: ' + e.message;
    } finally {
      loadingMore = false;
    }
  }

  function azzera() {
    keyword = ''; generiSel = []; genreModeAnd = false;
    stagione = ''; anno = ''; tipo = ''; stato = ''; studio = '';
    lingua = ''; soloDoppiati = false; ordine = 1;
    items = []; page = 0; maxPage = 1; searched = false; error = '';
  }

  async function apriDettaglio(item) {
    detail = item;
    detailData = null;
    detailLoading = true;
    try {
      detailData = await api.discover.tooltip(item.tooltipId);
    } catch {
      detailData = { trama: '', voto: '', stato: '', dataUscita: '', nomiAlternativi: '', generi: [] };
    } finally {
      detailLoading = false;
    }
  }

  function aggiungiDaScopri(item) {
    form = {
      service: 'animeW_scraper',
      name: item.titolo,
      path: '',
      url: ANIMEWORLD_BASE + item.pageUrl,
      continue: false,
      highPriority: false,
      passedEpisodes: 0,
    };
    detail = null;
    showForm = true;
    notice = '';
  }

  async function salvaForm() {
    if (!form.name.trim() || !form.path.trim() || !form.url.trim()) {
      error = 'Nome, Percorso e URL sono obbligatori.';
      return;
    }
    error = '';
    try {
      await api.series.add({
        name: form.name.trim(),
        service: form.service,
        path: form.path.trim(),
        series_page_url: form.url.trim(),
        continue: form.continue,
        is_high_priority: form.highPriority,
        passed_episodes: form.passedEpisodes,
        episode_list_selector: '',
        download_link_selector: '',
        fetch_metadata: form.fetchMetadata !== false,
      });
      showForm = false;
      notice = `“${form.name}” aggiunta: la troverai in Gestione Serie.`;
    } catch (e) {
      error = e.message;
    }
  }

  onMount(() => { cerca(); });
</script>

<div class="discover-page">
  <h2>Scopri</h2>

  <form class="discover-filters" onsubmit={(e) => { e.preventDefault(); cerca(); }}>
    <input type="search" class="filter-keyword" placeholder="Cerca per titolo..." bind:value={keyword} aria-label="Cerca per titolo" />
    <div class="filter-row">
      <label>Tipo
        <select bind:value={tipo}>
          <option value="">Tutti</option>
          {#each TIPI as [v, l]}<option value={v}>{l}</option>{/each}
        </select>
      </label>
      <label>Stato
        <select bind:value={stato}>
          <option value="">Tutti</option>
          {#each STATI as [v, l]}<option value={v}>{l}</option>{/each}
        </select>
      </label>
      <label>Stagione
        <select bind:value={stagione}>
          <option value="">Tutte</option>
          {#each STAGIONI as [v, l]}<option value={v}>{l}</option>{/each}
        </select>
      </label>
      <label>Anno
        <select bind:value={anno}>
          <option value="">Tutti</option>
          {#each ANNI as y}<option value={y}>{y}</option>{/each}
        </select>
      </label>
      <label>Audio
        <select bind:value={lingua}>
          <option value="">Tutti</option>
          {#each LINGUE as [v, l]}<option value={v}>{l}</option>{/each}
        </select>
      </label>
      <label>Ordine
        <select bind:value={ordine}>
          {#each ORDINI as [v, l]}<option value={v}>{l}</option>{/each}
        </select>
      </label>
      <label class="check"><input type="checkbox" bind:checked={soloDoppiati} /> Doppiati</label>
    </div>
    <details class="filter-genres">
      <summary>Generi{#if generiSel.length} ({generiSel.length}){/if}</summary>
      <div class="genres-grid">
        {#each GENERI as [v, l]}
          <label class="check"><input type="checkbox" value={v} checked={generiSel.includes(v)} onchange={(e) => { generiSel = e.target.checked ? [...generiSel, v] : generiSel.filter(g => g !== v); }} /> {l}</label>
        {/each}
      </div>
      {#if generiSel.length > 1}
        <label class="check"><input type="checkbox" bind:checked={genreModeAnd} /> Tutti i generi insieme (AND)</label>
      {/if}
    </details>
    <div class="filter-row">
      <label class="studio-label">Studio
        <input type="text" placeholder="es. MAPPA" bind:value={studio} />
      </label>
      <button type="submit" class="btn-primary" disabled={loading}>{loading ? 'Ricerca...' : 'Cerca'}</button>
      <button type="button" class="btn-ghost" onclick={azzera}>Azzera</button>
    </div>
  </form>

  {#if error}<p class="discover-error" role="alert">{error}</p>{/if}
  {#if notice}<p class="discover-notice" role="status">{notice}</p>{/if}

  {#if searched && !loading && items.length === 0 && !error}
    <p class="discover-empty">Nessun risultato con questi filtri.</p>
  {/if}

  <div class="discover-grid">
    {#each items as item, i (item.pageUrl)}
      <div class="discover-card" style="--i:{Math.min(i, 20)}" role="button" tabindex="0"
        onclick={() => apriDettaglio(item)} onkeydown={(e) => e.key === 'Enter' && apriDettaglio(item)} aria-label={item.titolo}>
        <div class="discover-poster-wrap">
          <img class="discover-poster" src={item.posterUrl} alt="" loading="lazy" decoding="async" />
          <div class="discover-title-overlay"><h3>{item.titolo}</h3></div>
          <div class="discover-hover-actions">
            <button type="button" class="btn-hover" title="Dettagli" aria-label="Dettagli"
              onclick={(e) => { e.stopPropagation(); apriDettaglio(item); }}>
              <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="9"/><line x1="12" y1="11" x2="12" y2="16"/><line x1="12" y1="8" x2="12.01" y2="8"/></svg>
            </button>
            <button type="button" class="btn-hover" title="Aggiungi" aria-label="Aggiungi"
              onclick={(e) => { e.stopPropagation(); aggiungiDaScopri(item); }}>
              <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><line x1="12" y1="5" x2="12" y2="19"/><line x1="5" y1="12" x2="19" y2="12"/></svg>
            </button>
          </div>
        </div>
      </div>
    {/each}
  </div>

  {#if searched && page < maxPage}
    <div class="discover-more">
      <button type="button" class="btn-ghost" onclick={caricaAltri} disabled={loadingMore}>
        {loadingMore ? 'Caricamento...' : `Carica altri (pagina ${page + 1} di ${maxPage})`}
      </button>
    </div>
  {/if}
</div>

{#if detail}
  <div class="modal-overlay" onclick={() => detail = null} onkeydown={(e) => e.key === 'Escape' && (detail = null)} role="dialog" aria-modal="true" tabindex="-1">
    <div class="modal-panel detail-modal" onclick={(e) => e.stopPropagation()} role="presentation">
      <div class="detail-modal-inner">
        <div class="detail-poster-col">
          <img class="detail-poster" src={detail.posterUrl} alt="" loading="lazy" decoding="async" />
        </div>
        <div class="detail-info-col">
          <h3>{detail.titolo}</h3>
          {#if detailLoading}
            <p class="detail-loading">Caricamento dettagli...</p>
          {:else if detailData}
            {#if detailData.trama}<p class="detail-plot">{detailData.trama}</p>{/if}
            <dl class="detail-meta">
              {#if detailData.stato}<div><dt>Stato</dt><dd>{detailData.stato}</dd></div>{/if}
              {#if detailData.voto}<div><dt>Voto</dt><dd>{detailData.voto}</dd></div>{/if}
              {#if detailData.dataUscita}<div><dt>Uscita</dt><dd>{detailData.dataUscita}</dd></div>{/if}
              {#if detailData.generi?.length}<div><dt>Generi</dt><dd>{detailData.generi.join(', ')}</dd></div>{/if}
              {#if detailData.nomiAlternativi}<div><dt>Altri nomi</dt><dd>{detailData.nomiAlternativi}</dd></div>{/if}
            </dl>
          {/if}
          <div class="detail-actions">
            <button type="button" class="btn-primary" onclick={() => aggiungiDaScopri(detail)}>Aggiungi</button>
            <button type="button" class="btn-ghost" onclick={() => detail = null}>Chiudi</button>
          </div>
        </div>
      </div>
    </div>
  </div>
{/if}

{#if showForm}
  <AddSeriesForm
    show={showForm}
    {form}
    editing={-1}
    series={[]}
    backToName="Scopri"
    onclose={() => showForm = false}
    onsave={salvaForm}
    onpick={() => showBrowser = true}
    onerror={(msg) => error = msg}
  />
{/if}

<DirectoryBrowser
  show={showBrowser}
  currentPath={form.path || '~/Video'}
  title="Scegli cartella per la serie"
  onselect={(p) => { form.path = p; showBrowser = false; }}
  oncancel={() => showBrowser = false}
/>

<style>
  .discover-page { display: flex; flex-direction: column; gap: 1rem; }
  .discover-page h2 { font-size: 1.4rem; }
  .discover-filters { background: var(--bg-secondary); border: 1px solid var(--border-color); border-radius: 12px; padding: 1rem; display: flex; flex-direction: column; gap: 0.75rem; }
  .filter-keyword { padding: 0.6rem 0.8rem; border-radius: 8px; border: 1px solid var(--border-color); background: var(--bg-tertiary); color: var(--text-primary); font-size: 1rem; }
  .filter-row { display: flex; flex-wrap: wrap; gap: 0.6rem; align-items: end; }
  .filter-row label { display: flex; flex-direction: column; gap: 0.25rem; font-size: 0.75rem; color: var(--text-secondary); }
  .filter-row select, .studio-label input { padding: 0.45rem 0.6rem; border-radius: 8px; border: 1px solid var(--border-color); background: var(--bg-tertiary); color: var(--text-primary); }
  .check { flex-direction: row !important; align-items: center; gap: 0.35rem !important; font-size: 0.85rem !important; }
  .filter-genres summary { cursor: pointer; color: var(--text-secondary); font-size: 0.9rem; }
  .genres-grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(140px, 1fr)); gap: 0.25rem 0.75rem; margin-top: 0.5rem; }
  .btn-primary { padding: 0.55rem 1.2rem; border-radius: 8px; border: none; background: var(--accent); color: #fff; cursor: pointer; font-size: 0.9rem; }
  .btn-primary:hover { background: var(--accent-hover); }
  .btn-primary:disabled { opacity: 0.6; cursor: wait; }
  .btn-ghost { padding: 0.55rem 1rem; border-radius: 8px; border: 1px solid var(--border-color); background: transparent; color: var(--text-secondary); cursor: pointer; }
  .btn-ghost:hover { background: var(--bg-tertiary); color: var(--text-primary); }
  .discover-error { color: var(--danger); }
  .discover-notice { color: var(--success); }
  .discover-empty { color: var(--text-muted); }
  .discover-grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(160px, 1fr)); gap: 1rem; }
  .discover-card { background: var(--bg-secondary); border: 1px solid var(--border-color); border-radius: 12px; overflow: hidden; animation: cardIn 0.35s ease-out both; animation-delay: calc(var(--i, 0) * 40ms); }
  .discover-poster-wrap { position: relative; overflow: hidden; cursor: pointer; }
  .discover-poster { width: 100%; height: auto; aspect-ratio: 2 / 3; object-fit: contain; background: var(--bg-tertiary); display: block; }
  .discover-title-overlay { position: absolute; left: 0; right: 0; bottom: 0; padding: 1.6rem 0.6rem 0.55rem; background: linear-gradient(to top, rgba(0,0,0,0.88) 0%, rgba(0,0,0,0.45) 55%, transparent 100%); color: #fff; }
  .discover-title-overlay h3 { font-size: 0.85rem; font-weight: 600; display: -webkit-box; -webkit-line-clamp: 2; -webkit-box-orient: vertical; overflow: hidden; }
  .discover-hover-actions { position: absolute; inset: 0; display: flex; align-items: center; justify-content: center; gap: 0.6rem; background: rgba(0,0,0,0.45); opacity: 0; transition: opacity 0.2s ease; }
  @media (hover: hover) { .discover-card:hover .discover-hover-actions { opacity: 1; } }
  .btn-hover { width: 2.5rem; height: 2.5rem; border-radius: 50%; border: 1px solid rgba(255,255,255,0.35); background: rgba(20,20,25,0.75); color: #fff; cursor: pointer; display: flex; align-items: center; justify-content: center; }
  .btn-hover:hover { background: var(--accent); border-color: var(--accent); }
  .btn-hover svg { width: 1.1rem; height: 1.1rem; }
  .discover-more { display: flex; justify-content: center; padding: 0.5rem 0 1rem; }
  .modal-overlay { position: fixed; inset: 0; background: var(--overlay); display: flex; align-items: center; justify-content: center; z-index: 100; padding: 1rem; }
  .modal-panel { background: var(--bg-secondary); border: 1px solid var(--border-color); border-radius: 12px; max-width: 640px; width: 100%; max-height: 90vh; overflow: auto; padding: 1.25rem; }
  .detail-modal-inner { display: flex; gap: 1rem; }
  .detail-poster { width: 160px; border-radius: 8px; }
  .detail-info-col { flex: 1; display: flex; flex-direction: column; gap: 0.6rem; min-width: 0; }
  .detail-plot { color: var(--text-secondary); font-size: 0.9rem; }
  .detail-meta { display: flex; flex-direction: column; gap: 0.2rem; font-size: 0.85rem; }
  .detail-meta div { display: flex; gap: 0.5rem; }
  .detail-meta dt { color: var(--text-muted); min-width: 80px; }
  .detail-meta dd { margin: 0; color: var(--text-primary); }
  .detail-actions { display: flex; gap: 0.5rem; margin-top: 0.4rem; }
  .detail-loading { color: var(--text-muted); }
  @keyframes cardIn { from { opacity: 0; transform: translateY(12px); } to { opacity: 1; transform: translateY(0); } }
  @media (max-width: 640px) { .detail-modal-inner { flex-direction: column; } .detail-poster { width: 120px; } }
</style>
