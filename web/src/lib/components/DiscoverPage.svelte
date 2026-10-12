<script>
  // Pagina Scopri: ricerca e filtri sul catalogo AnimeWorld (come la pagina
  // /filter del sito), collezione con gli stessi componenti di Gestione
  // (SeriesCollection/SeriesCard/SeriesRow/DetailModal), dettaglio lazy via
  // tooltip, Aggiungi con form precompilato. Paginazione a chunk da ~40
  // (una pagina del sito) con "carica altri": niente scarichi massivi.
  // Badge "In gestione" e guard anti-duplicati da identity opache del server.
  import { onMount } from 'svelte';
  import { api, isSeriesDuplicateError } from '../api.js';
  import { creaStatoGestite } from '../managedSeriesState.js';
  import AddSeriesForm from './AddSeriesForm.svelte';
  import DirectoryBrowser from './DirectoryBrowser.svelte';
  import SeriesCollection from './SeriesCollection.svelte';
  import SeriesCard from './SeriesCard.svelte';
  import SeriesRow from './SeriesRow.svelte';
  import DetailModal from './DetailModal.svelte';

  const ANIMEWORLD_BASE = 'https://www.animeworld.ac';
  const VISTA_KEY = 'anidl.discover.view';

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

  // Vista griglia/grande/tabella come Gestione, preferenza separata.
  let viewMode = $state('grid');
  try {
    const v = localStorage.getItem(VISTA_KEY);
    if (v === 'grid' || v === 'large' || v === 'table') viewMode = v;
  } catch {}
  function setViewMode(v) {
    viewMode = v;
    try { localStorage.setItem(VISTA_KEY, v); } catch {}
  }
  let large = $derived(viewMode === 'large');

  let detail = $state(null);
  let detailData = $state(null);
  let detailLoading = $state(false);
  let detailError = $state('');

  let showForm = $state(false);
  let showBrowser = $state(false);
  let saving = $state(false);
  let form = $state({ service: 'animeW_scraper', name: '', path: '', url: '', continue: false, highPriority: false, passedEpisodes: 0 });
  let formSourceIdentity = $state('');
  let notice = $state('');

  // Serie in gestione: unica fonte per badge e guard (identity dal server).
  let gestite = $state(creaStatoGestite());
  let gestiteRefresh = $state(false);

  // Generazioni anti-gara: risposte fuori ordine non sovrascrivono mai
  // risultati più recenti (filtri cambiati, tooltip A->B, refetch gestite).
  let catalogSeq = 0;
  let detailSeq = 0;
  let gestiteSeq = 0;

  function chiaveItem(it) {
    return it.identity || it.pageUrl;
  }

  function inGestione(item) {
    return !!item && gestite.gestita(item.identity);
  }

  function aggiuntaAbilitata(item) {
    return !!item && gestite.aggiuntaConsentita(item.identity);
  }

  function titoloAggiungi(item) {
    if (!item) return 'Aggiungi';
    if (gestite.stato !== 'ready') return 'Elenco serie in caricamento...';
    if (inGestione(item)) return 'Già in gestione';
    return 'Aggiungi';
  }

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

  function accodaUnici(correnti, nuovi) {
    const visti = new Set(correnti.map(chiaveItem));
    const out = [...correnti];
    for (const it of nuovi) {
      const k = chiaveItem(it);
      if (!visti.has(k)) {
        visti.add(k);
        out.push(it);
      }
    }
    return out;
  }

  async function cerca() {
    const seq = ++catalogSeq;
    loading = true;
    error = '';
    notice = '';
    try {
      const data = await api.discover.page(buildOpts(1));
      if (seq !== catalogSeq) return; // filtri cambiati nel frattempo
      items = data.items || [];
      page = data.page || 1;
      maxPage = data.maxPage || 1;
      searched = true;
    } catch (e) {
      if (seq !== catalogSeq) return;
      error = 'Ricerca fallita: ' + e.message;
    } finally {
      if (seq === catalogSeq) loading = false;
    }
  }

  async function caricaAltri() {
    if (loadingMore || page >= maxPage) return;
    const seq = catalogSeq;
    loadingMore = true;
    try {
      const data = await api.discover.page(buildOpts(page + 1));
      if (seq !== catalogSeq) return; // filtri cambiati: scarta la pagina vecchia
      items = accodaUnici(items, data.items || []);
      page = data.page || page + 1;
      maxPage = data.maxPage || maxPage;
    } catch (e) {
      if (seq !== catalogSeq) return;
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
    catalogSeq++; // invalida risposte in volo della ricerca precedente
  }

  async function caricaGestite() {
    const seq = ++gestiteSeq;
    gestiteRefresh = true;
    try {
      const data = await api.series.list();
      if (seq !== gestiteSeq) return;
      gestite.carica(data.series || []);
    } catch (e) {
      if (seq !== gestiteSeq) return;
      gestite.fallimento('Elenco serie non disponibile: ' + e.message);
    } finally {
      if (seq === gestiteSeq) gestiteRefresh = false;
    }
  }

  async function apriDettaglio(item) {
    const seq = ++detailSeq;
    detail = item;
    detailData = null;
    detailLoading = true;
    detailError = '';
    try {
      const data = await api.discover.tooltip(item.tooltipId);
      if (seq !== detailSeq) return; // altro dettaglio aperto nel frattempo
      detailData = data;
    } catch (e) {
      if (seq !== detailSeq) return;
      detailError = 'Dettaglio non disponibile: ' + e.message;
      detailData = null;
    } finally {
      if (seq === detailSeq) detailLoading = false;
    }
  }

  function aggiungiDaScopri(item) {
    if (!item) return;
    if (gestite.stato !== 'ready') {
      notice = '';
      error = 'Attendi il caricamento delle serie in gestione prima di aggiungere.';
      return;
    }
    if (inGestione(item)) {
      notice = `“${item.titolo}” è già in gestione: niente doppia aggiunta.`;
      return;
    }
    form = {
      service: 'animeW_scraper',
      name: item.titolo,
      path: '',
      url: ANIMEWORLD_BASE + item.pageUrl,
      continue: false,
      highPriority: false,
      passedEpisodes: 0,
    };
    formSourceIdentity = item.identity || '';
    if (formSourceIdentity) gestite.iniziaSalvataggio(formSourceIdentity);
    detail = null;
    showForm = true;
    notice = '';
    error = '';
  }

  function chiudiForm() {
    showForm = false;
    if (formSourceIdentity) gestite.fineSalvataggio(formSourceIdentity);
    formSourceIdentity = '';
  }

  async function salvaForm() {
    // Guard sincrona anti-doppio-click: la seconda chiamata termina subito.
    if (saving) return;
    if (!form.name.trim() || !form.path.trim() || !form.url.trim()) {
      error = 'Nome, Percorso e URL sono obbligatori.';
      return;
    }
    error = '';
    saving = true;
    try {
      const data = await api.series.add({
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
      // Identity di RISPOSTA: il form può aver cambiato l'URL originale.
      gestite.aggiuntaRiuscita(data && data.identity ? data.identity : formSourceIdentity);
      const nome = form.name;
      chiudiForm();
      await caricaGestite();
      notice = `“${nome}” aggiunta: la troverai in Gestione Serie.`;
    } catch (e) {
      if (isSeriesDuplicateError(e)) {
        // Altro client/scheda l'ha aggiunta prima: riallinea senza falso successo.
        if (e.identity) gestite.conflitto(e.identity);
        error = `“${form.name}” è già in gestione: niente doppia aggiunta.`;
        try { await caricaGestite(); }
        catch { /* conflitto noto già memorizzato */ }
      } else {
        error = e.message;
      }
    } finally {
      saving = false;
    }
  }

  onMount(() => {
    cerca();
    caricaGestite();
    // Rientro pagina/scheda: riallinea l'indice gestite (deduplica con
    // le richieste in volo, niente polling per card).
    let inRefresh = false;
    const riallinea = () => {
      if (inRefresh || document.hidden) return;
      inRefresh = true;
      caricaGestite().finally(() => { inRefresh = false; });
    };
    window.addEventListener('focus', riallinea);
    window.addEventListener('pageshow', riallinea);
    document.addEventListener('visibilitychange', riallinea);
    return () => {
      window.removeEventListener('focus', riallinea);
      window.removeEventListener('pageshow', riallinea);
      document.removeEventListener('visibilitychange', riallinea);
    };
  });
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
  {#if gestite.stato === 'error'}
    <p class="discover-error" role="alert">{gestite.errore}
      <button type="button" class="btn-ghost" onclick={caricaGestite} disabled={gestiteRefresh}>Riprova</button>
    </p>
  {/if}

  <div class="view-toggle" role="group" aria-label="Vista risultati">
    <button type="button" class:active={viewMode === 'grid'} onclick={() => setViewMode('grid')} title="Vista griglia" aria-label="Vista griglia" aria-pressed={viewMode === 'grid'}>
      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" width="16" height="16"><rect x="3" y="3" width="7" height="7"/><rect x="14" y="3" width="7" height="7"/><rect x="3" y="14" width="7" height="7"/><rect x="14" y="14" width="7" height="7"/></svg>
    </button>
    <button type="button" class:active={viewMode === 'table'} onclick={() => setViewMode('table')} title="Vista tabella" aria-label="Vista tabella" aria-pressed={viewMode === 'table'}>
      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" width="16" height="16"><rect x="3" y="3" width="18" height="4"/><rect x="3" y="10" width="18" height="4"/><rect x="3" y="17" width="18" height="4"/></svg>
    </button>
    <button type="button" class:active={viewMode === 'large'} onclick={() => setViewMode('large')} title="Vista grande" aria-label="Vista grande" aria-pressed={viewMode === 'large'}>
      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" width="16" height="16"><path d="M15 3h6v6M9 21H3v-6M21 3l-7 7M3 21l7-7"/></svg>
    </button>
  </div>

  <SeriesCollection
    {items}
    keyOf={chiaveItem}
    {viewMode}
    emptyText="Nessun risultato con questi filtri."
    showEmpty={searched && !loading && !error}
  >
    {#snippet card(item, i)}
      <SeriesCard
        title={item.titolo}
        poster={item.posterUrl}
        {large}
        index={Math.min(i, 20)}
        onopen={() => apriDettaglio(item)}
      >
        {#snippet meta()}
          <span class="card-service">AnimeW</span>
          {#if inGestione(item)}
            <span class="card-managed">In gestione</span>
          {/if}
        {/snippet}
        {#snippet actions()}
          {#if inGestione(item)}
            <button type="button" class="btn-hover" title="Già in gestione" aria-label="Già in gestione" disabled>
              <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><polyline points="20 6 9 17 4 12"/></svg>
            </button>
          {:else}
            <button type="button" class="btn-hover" title={titoloAggiungi(item)} aria-label="Aggiungi"
              disabled={!aggiuntaAbilitata(item)}
              onclick={(e) => { e.stopPropagation(); aggiungiDaScopri(item); }}>
              <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><line x1="12" y1="5" x2="12" y2="19"/><line x1="5" y1="12" x2="19" y2="12"/></svg>
            </button>
          {/if}
        {/snippet}
      </SeriesCard>
    {/snippet}
    {#snippet row(item, i)}
      <SeriesRow
        title={item.titolo}
        thumb={item.posterUrl}
        index={Math.min(i, 20)}
        onopen={() => apriDettaglio(item)}
      >
        {#snippet meta()}
          <span class="row-service">AnimeW</span>
        {/snippet}
        {#snippet badge()}
          {#if inGestione(item)}
            <span class="row-badge row-badge-managed">In gestione</span>
          {/if}
        {/snippet}
        {#snippet actions()}
          {#if inGestione(item)}
            <button type="button" class="btn-row" title="Già in gestione" aria-label="Già in gestione" disabled>
              <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" width="14" height="14"><polyline points="20 6 9 17 4 12"/></svg>
            </button>
          {:else}
            <button type="button" class="btn-row" title={titoloAggiungi(item)} aria-label="Aggiungi"
              disabled={!aggiuntaAbilitata(item)}
              onclick={(e) => { e.stopPropagation(); aggiungiDaScopri(item); }}>
              <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" width="14" height="14"><line x1="12" y1="5" x2="12" y2="19"/><line x1="5" y1="12" x2="19" y2="12"/></svg>
            </button>
          {/if}
        {/snippet}
      </SeriesRow>
    {/snippet}
  </SeriesCollection>

  {#if searched && page < maxPage}
    <div class="discover-more">
      <button type="button" class="btn-ghost" onclick={caricaAltri} disabled={loadingMore}>
        {loadingMore ? 'Caricamento...' : `Carica altri (pagina ${page + 1} di ${maxPage})`}
      </button>
    </div>
  {/if}
</div>

{#if detail}
  <DetailModal
    title={detail.titolo}
    poster={detail.posterUrl}
    onclose={() => { detailSeq++; detail = null; }}
  >
    {#snippet meta()}
      <span class="card-service">AnimeW</span>
      {#if inGestione(detail)}
        <span class="detail-badge detail-badge-managed">In gestione</span>
      {/if}
    {/snippet}
    {#snippet body()}
      {#if detailLoading}
        <div class="detail-skeleton" role="status" aria-label="Caricamento dettagli">
          <span class="sk sk-plot" style="width:95%" aria-hidden="true"></span>
          <span class="sk sk-plot" style="width:88%" aria-hidden="true"></span>
          <span class="sk sk-plot" style="width:72%" aria-hidden="true"></span>
          <span class="sk sk-plot" style="width:60%" aria-hidden="true"></span>
          <span class="sk sk-meta" style="width:75%" aria-hidden="true"></span>
          <span class="sk sk-meta" style="width:65%" aria-hidden="true"></span>
        </div>
      {:else if detailError}
        <p class="detail-error" role="alert">{detailError}</p>
        <button type="button" class="btn-ghost" onclick={() => apriDettaglio(detail)}>Riprova</button>
      {:else if detailData}
        {#if detailData.trama}<p class="detail-desc">{detailData.trama}</p>{/if}
      {:else}
        <p class="detail-desc detail-desc-empty">Nessuna descrizione disponibile.</p>
      {/if}
    {/snippet}
    {#snippet details()}
      {#if detailData && !detailLoading && !detailError}
        <dl class="detail-meta-list">
          {#if detailData.stato}<div><dt>Stato</dt><dd>{detailData.stato}</dd></div>{/if}
          {#if detailData.voto}<div><dt>Voto</dt><dd>{detailData.voto}</dd></div>{/if}
          {#if detailData.dataUscita}<div><dt>Uscita</dt><dd>{detailData.dataUscita}</dd></div>{/if}
          {#if detailData.generi?.length}<div><dt>Generi</dt><dd>{detailData.generi.join(', ')}</dd></div>{/if}
          {#if detailData.nomiAlternativi}<div><dt>Altri nomi</dt><dd>{detailData.nomiAlternativi}</dd></div>{/if}
        </dl>
      {/if}
    {/snippet}
    {#snippet actions()}
      <div class="detail-actions">
        {#if inGestione(detail)}
          <button type="button" class="btn-primary" disabled title="Già in gestione">In gestione</button>
        {:else}
          <button type="button" class="btn-primary" disabled={!aggiuntaAbilitata(detail)} title={titoloAggiungi(detail)} onclick={() => aggiungiDaScopri(detail)}>Aggiungi</button>
        {/if}
        <button type="button" class="btn-ghost" onclick={() => { detailSeq++; detail = null; }}>Chiudi</button>
      </div>
    {/snippet}
  </DetailModal>
{/if}

{#if showForm}
  <AddSeriesForm
    show={showForm}
    {form}
    editing={-1}
    series={[]}
    backToName="Scopri"
    busy={saving}
    onclose={chiudiForm}
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
  .discover-page { display: flex; flex-direction: column; gap: 1rem; min-width: 0; }
  .discover-page h2 { font-size: 1.4rem; }
  .discover-filters { background: var(--bg-secondary); border: 1px solid var(--border-color); border-radius: 12px; padding: 1rem; display: flex; flex-direction: column; gap: 0.75rem; min-width: 0; }
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
  .view-toggle { display: flex; gap: 0.4rem; }
  .view-toggle button {
    background: none; border: 1px solid var(--border-color); border-radius: 8px;
    color: var(--text-muted); padding: 0.4rem 0.55rem; cursor: pointer;
    display: flex; align-items: center; justify-content: center;
  }
  .view-toggle button:hover { color: var(--text-primary); border-color: var(--accent); }
  .view-toggle button.active { color: var(--accent); border-color: var(--accent); }
  .discover-error { color: var(--danger); }
  .discover-notice { color: var(--success); }
  .discover-more { display: flex; justify-content: center; padding: 0.5rem 0 1rem; }
  /* Skeleton del dettaglio in caricamento: occupa circa quanto trama+meta
     reali, così il modale (ora a larghezza fissa) non scatta in altezza
     all'arrivo del tooltip. Solo Scopri: Gestione ha la descrizione pronta. */
  .detail-skeleton { display: flex; flex-direction: column; gap: 0.6rem; min-height: 240px; }
  .detail-skeleton .sk {
    display: block; border-radius: 6px;
    background: linear-gradient(90deg, var(--bg-tertiary) 25%, var(--border-color) 50%, var(--bg-tertiary) 75%);
    background-size: 200% 100%;
    animation: skShimmer 1.2s linear infinite;
  }
  .detail-skeleton .sk-plot { height: 0.9rem; }
  .detail-skeleton .sk-meta { height: 0.85rem; }
  @keyframes skShimmer { from { background-position: 200% 0; } to { background-position: -200% 0; } }
  @media (prefers-reduced-motion: reduce) {
    .detail-skeleton .sk { animation: none; }
  }
</style>
