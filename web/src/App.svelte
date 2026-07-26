<script>
  import { onMount } from 'svelte';
  import { initTheme } from './lib/theme.svelte.js';
  import HomePage from './lib/components/StatusPage.svelte';
  import SeriesManagerPage from './lib/components/DashboardPage.svelte';
  import ConfigPage from './lib/components/ConfigPage.svelte';
  import LogsPage from './lib/components/LogsPage.svelte';

  const routeMap = {
    '/': 'home', '/gestione': 'gestione', '/config': 'config', '/logs': 'logs',
  };

  let route = $state(routeMap[window.location.pathname] || 'home');

  function navigateTo(r) {
    route = r;
    const path = r === 'home' ? '/' : '/' + r;
    history.pushState(null, '', path);
  }

  onMount(() => {
    initTheme();
    window.addEventListener('popstate', () => {
      route = routeMap[window.location.pathname] || 'home';
    });
  });
</script>
  <div class="app-layout">
    <nav class="sidebar">
      <div class="sidebar-header">
        <img src="/logo.png" class="sidebar-logo" alt="AniDownloader" />
        <h1>AniDownloader</h1>
      </div>
      <div class="nav-items">
        <button class="nav-item" class:active={route === 'home'} onclick={() => navigateTo('home')}>
          <svg class="nav-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M21 15v4a2 2 0 01-2 2H5a2 2 0 01-2-2v-4"/><polyline points="7 10 12 15 17 10"/><line x1="12" y1="15" x2="12" y2="3"/></svg>
          Home
        </button>
        <button class="nav-item" class:active={route === 'gestione'} onclick={() => navigateTo('gestione')}>
          <svg class="nav-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><line x1="8" y1="6" x2="21" y2="6"/><line x1="8" y1="12" x2="21" y2="12"/><line x1="8" y1="18" x2="21" y2="18"/><line x1="3" y1="6" x2="3.01" y2="6"/><line x1="3" y1="12" x2="3.01" y2="12"/><line x1="3" y1="18" x2="3.01" y2="18"/></svg>
          Gestione Serie
        </button>
        <button class="nav-item" class:active={route === 'config'} onclick={() => navigateTo('config')}>
          <svg class="nav-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="3"/><path d="M19.4 15a1.65 1.65 0 00.33 1.82l.06.06a2 2 0 010 2.83 2 2 0 01-2.83 0l-.06-.06a1.65 1.65 0 00-1.82-.33 1.65 1.65 0 00-1 1.51V21a2 2 0 01-2 2 2 2 0 01-2-2v-.09A1.65 1.65 0 009 19.4a1.65 1.65 0 00-1.82.33l-.06.06a2 2 0 01-2.83 0 2 2 0 010-2.83l.06-.06A1.65 1.65 0 004.68 15a1.65 1.65 0 00-1.51-1H3a2 2 0 01-2-2 2 2 0 012-2h.09A1.65 1.65 0 004.6 9a1.65 1.65 0 00-.33-1.82l-.06-.06a2 2 0 010-2.83 2 2 0 012.83 0l.06.06A1.65 1.65 0 009 4.68a1.65 1.65 0 001-1.51V3a2 2 0 012-2 2 2 0 012 2v.09a1.65 1.65 0 001 1.51 1.65 1.65 0 001.82-.33l.06-.06a2 2 0 012.83 0 2 2 0 010 2.83l-.06.06a1.65 1.65 0 00-.33 1.82V9a1.65 1.65 0 001.51 1H21a2 2 0 012 2 2 2 0 01-2 2h-.09a1.65 1.65 0 00-1.51 1z"/></svg>
          Impostazioni
        </button>
        <button class="nav-item" class:active={route === 'logs'} onclick={() => navigateTo('logs')}>
          <svg class="nav-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M14 2H6a2 2 0 00-2 2v16a2 2 0 002 2h12a2 2 0 002-2V8z"/><polyline points="14 2 14 8 20 8"/><line x1="16" y1="13" x2="8" y2="13"/><line x1="16" y1="17" x2="8" y2="17"/><polyline points="10 9 9 9 8 9"/></svg>
          Logs
        </button>
      </div>
    </nav>
    <main class="content">
      {#key route}
        {#if route === 'home'}
          <HomePage />
        {:else if route === 'gestione'}
          <SeriesManagerPage />
        {:else if route === 'config'}
          <ConfigPage />
        {:else if route === 'logs'}
          <LogsPage />
        {/if}
      {/key}
    </main>
  </div>

<style>
  :global(*) {
    margin: 0;
    padding: 0;
    box-sizing: border-box;
  }

  :global(:root), :global([data-theme="dark"]) {
    --bg-primary: #0f0f0f;
    --bg-secondary: #1a1a1a;
    --bg-tertiary: #252525;
    --bg-hover: #333;
    --border-color: #2a2a2a;
    --text-primary: #e0e0e0;
    --text-secondary: #aaa;
    --text-muted: #666;
    --text-muted-more: #555;
    --accent: #7c3aed;
    --accent-hover: #6d28d9;
    --accent-light: #a78bfa;
    --accent-bg: #2d1b69;
    --danger: #f87171;
    --danger-bg: #3d1a1a;
    --danger-border: #6b2a2a;
    --danger-bg-hover: #8c3a3a;
    --danger-card-hover: #2a1a1a;
    --success: #4ade80;
    --success-border: #2a6b2a;
    --warning: #facc15;
    --warning-bg: #2a2010;
    --warning-border: #5a4010;
    --warning-text: #d4a040;
    --overlay: rgba(0,0,0,0.6);
    --log-bg: #0d0d0d;
    --poster-frame-bg: #121212;
    --card-service-bg: #2d1b69;
    --scrollbar-thumb: #333;
    --scrollbar-track: #1a1a1a;
    --btn-cancel-border: #333;
  }

  :global([data-theme="light"]) {
    --bg-primary: #f5f5f5;
    --bg-secondary: #ffffff;
    --bg-tertiary: #e8e8e8;
    --bg-hover: #d0d0d0;
    --border-color: #d0d0d0;
    --text-primary: #222;
    --text-secondary: #666;
    --text-muted: #999;
    --text-muted-more: #bbb;
    --accent: #7c3aed;
    --accent-hover: #6d28d9;
    --accent-light: #7c3aed;
    --accent-bg: #ede9fe;
    --danger: #dc2626;
    --danger-bg: #fee2e2;
    --danger-border: #fca5a5;
    --danger-bg-hover: #fecaca;
    --danger-card-hover: #fef2f2;
    --success: #16a34a;
    --success-border: #86efac;
    --warning: #ca8a04;
    --warning-bg: #fef9c3;
    --warning-border: #facc15;
    --warning-text: #854d0e;
    --overlay: rgba(0,0,0,0.3);
    --log-bg: #f0f0f0;
    --poster-frame-bg: #e8e8e8;
    --card-service-bg: #ede9fe;
    --scrollbar-thumb: #bbb;
    --scrollbar-track: #eee;
    --btn-cancel-border: #ccc;
  }

  :global(body) {
    font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Oxygen, Ubuntu, Cantarell, sans-serif;
    background: var(--bg-primary);
    color: var(--text-primary);
    min-height: 100vh;
  }

  :global(::-webkit-scrollbar) {
    width: 8px;
  }
  :global(::-webkit-scrollbar-track) {
    background: var(--scrollbar-track);
  }
  :global(::-webkit-scrollbar-thumb) {
    background: var(--scrollbar-thumb);
    border-radius: 4px;
  }

  .app-layout {
    display: flex;
    min-height: 100vh;
    position: relative;
  }

  .sidebar {
    width: 240px;
    background: var(--bg-secondary);
    border-right: 1px solid var(--border-color);
    display: flex;
    flex-direction: column;
    padding: 1.5rem;
    position: fixed;
    top: 0;
    left: 0;
    bottom: 0;
    z-index: 50;
  }

  .sidebar-header { display: flex; align-items: center; gap: 0.5rem; margin-bottom: 1.5rem; }
  .sidebar-logo { width: 26px; height: 26px; flex-shrink: 0; border-radius: 4px; }
  :global([data-theme="light"]) .sidebar-logo { filter: invert(1) brightness(0.8); }
  .sidebar-header h1 {
    font-size: 1.25rem;
    font-weight: 700;
    color: var(--accent);
  }

  .nav-items {
    flex: 1;
    display: flex;
    flex-direction: column;
    gap: 0.25rem;
  }

  .nav-item {
    background: none;
    border: none;
    color: var(--text-secondary);
    padding: 0.75rem 1rem;
    text-align: left;
    cursor: pointer;
    border-radius: 8px;
    font-size: 0.9rem;
    transition: all 0.15s;
    display: flex;
    align-items: center;
    gap: 0.6rem;
  }

  .nav-icon {
    width: 18px;
    height: 18px;
    flex-shrink: 0;
  }

  .nav-item:hover {
    background: var(--bg-tertiary);
    color: var(--text-primary);
  }

  .nav-item.active {
    background: var(--accent-bg);
    color: var(--accent-light);
  }

  .content {
    margin-left: 240px;
    padding: 2rem;
    flex: 1;
    width: calc(100% - 240px);
    position: relative;
    z-index: 1;
  }

  @media (max-width: 768px) {
    .sidebar {
      width: 100%;
      position: fixed;
      bottom: 0;
      top: auto;
      left: 0;
      right: 0;
      flex-direction: row;
      padding: 0.5rem 0.75rem;
      border-right: none;
      border-top: 1px solid var(--border-color);
      z-index: 100;
      height: auto;
    }

    .sidebar-header { display: none; }
    .nav-items { flex-direction: row; justify-content: space-around; gap: 0; }
    .nav-item {
      flex-direction: column;
      padding: 0.4rem 0.5rem;
      font-size: 0.65rem;
      gap: 0.15rem;
      border-radius: 6px;
    }
    .nav-icon { width: 20px; height: 20px; }
    .content {
      margin-left: 0;
      width: 100%;
      padding: 1rem 1rem 5rem;
    }
  }
</style>
