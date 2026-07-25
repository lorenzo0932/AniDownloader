<script>
  import { onMount } from 'svelte';
  import { fly } from 'svelte/transition';
  import { api } from '../api.js';

  let lines = $state([]);
  let count = $state(100);
  let error = $state('');
  let busy = $state(true);

  async function load() {
    busy = true;
    error = '';
    try {
      const data = await api.logs(count);
      lines = data.lines || [];
    } catch (e) {
      error = e.message;
    } finally {
      busy = false;
    }
  }

  function refresh() { load(); }
  onMount(load);
</script>

<div in:fly={{ y: 8, duration: 200 }}>
  <div class="header-row">
    <h2>Server Logs</h2>
    <div class="controls">
      <select bind:value={count} onchange={load}>
        <option value="50">50 lines</option>
        <option value="100">100 lines</option>
        <option value="200">200 lines</option>
        <option value="500">500 lines</option>
      </select>
      <button class="btn-primary" onclick={refresh}>Refresh</button>
    </div>
  </div>

  {#if error}
    <div class="error">{error}</div>
  {/if}

  {#if busy}
    <div class="center"><div class="spinner-sm"></div></div>
  {:else}
    <pre class="log-view">{#each lines as line}{line}
  {/each}</pre>
  {/if}
</div>

<style>
  .header-row { display: flex; justify-content: space-between; align-items: center; margin-bottom: 1.5rem; flex-wrap: wrap; gap: 1rem; }
  h2 { font-size: 1.5rem; font-weight: 700; }
  .controls { display: flex; gap: 0.5rem; align-items: center; }
  select {
    padding: 0.5rem 0.75rem; background: var(--bg-tertiary); border: 1px solid var(--border-color);
    border-radius: 8px; color: var(--text-primary); font-size: 0.85rem; cursor: pointer;
  }
  select:focus { outline: none; border-color: var(--accent); }
  .btn-primary {
    padding: 0.5rem 1.25rem; background: var(--accent); border: none; border-radius: 8px;
    color: #fff; font-size: 0.85rem; font-weight: 600; cursor: pointer;
  }
  .btn-primary:hover { background: var(--accent-hover); }
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
  .log-view {
    background: var(--log-bg); border: 1px solid var(--border-color); border-radius: 12px;
    padding: 1rem; font-family: 'Fira Code', 'Cascadia Code', monospace;
    font-size: 0.78rem; line-height: 1.5; color: var(--text-muted); max-height: 70vh;
    overflow-y: auto; white-space: pre-wrap; word-break: break-all;
    animation: fadeIn 0.3s ease-out;
  }
  @keyframes fadeIn {
    from { opacity: 0; transform: translateY(6px); }
    to { opacity: 1; transform: translateY(0); }
  }
</style>
