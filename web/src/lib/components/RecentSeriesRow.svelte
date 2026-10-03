<script>
  // Riga serie per le tabelle home (ultime aggiunte/scaricate): miniatura +
  // nome + servizio. Tutta la riga apre il dettaglio, come le righe della
  // tabella Gestione (stesso linguaggio in tutta l'app).
  import { posterUrl } from '../api.js';

  let { item, index = 0, onopen } = $props();
</script>

<div class="home-row" style="--i:{index}" role="button" tabindex="0"
  onclick={() => onopen?.(item._file_index)}
  onkeydown={(e) => e.key === 'Enter' && onopen?.(item._file_index)}
  aria-label={item.name}>
  <img class="home-poster" src={posterUrl(item.path, 96)} alt="" loading="lazy" decoding="async" width="40" height="56" />
  <div class="home-info">
    <span class="home-name">{item.name}</span>
    <span class="home-service">{item.service === 'animeU_scraper' ? 'AnimeU' : 'AnimeW'}</span>
  </div>
</div>

<style>
  .home-row {
    display: flex; align-items: center; gap: 0.75rem;
    background: var(--bg-secondary); border-radius: 8px; padding: 0.5rem 0.75rem;
    border: 1px solid var(--border-color); cursor: pointer;
    transition: border-color 0.15s ease;
    animation: rowIn 0.3s ease-out both;
    animation-delay: calc(var(--i, 0) * 20ms);
  }
  @media (hover: hover) {
    .home-row:hover { border-color: var(--accent); }
  }
  .home-row:focus-visible { outline: 2px solid var(--accent); outline-offset: 2px; }
  .home-poster {
    width: 40px; height: 56px; border-radius: 4px; object-fit: contain;
    background: var(--bg-tertiary); flex-shrink: 0;
  }
  .home-info { flex: 1; min-width: 0; display: flex; flex-direction: column; gap: 0.15rem; }
  .home-name {
    font-size: 0.85rem; font-weight: 500; color: var(--text-primary);
    white-space: nowrap; overflow: hidden; text-overflow: ellipsis;
  }
  .home-service { font-size: 0.68rem; color: var(--accent); }

  @keyframes rowIn {
    from { opacity: 0; transform: translateX(-8px); }
    to { opacity: 1; transform: translateX(0); }
  }

  @media (max-width: 768px) {
    .home-row { padding: 0.4rem 0.5rem; gap: 0.5rem; }
    .home-poster { width: 32px; height: 44px; }
    .home-name { font-size: 0.8rem; }
  }
</style>
