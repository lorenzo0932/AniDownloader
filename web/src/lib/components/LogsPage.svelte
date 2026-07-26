<script>
  import { onMount } from 'svelte';
  import { api } from '../api.js';
  import Dropdown from '../Dropdown.svelte';

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
      <Dropdown bind:value={count} options={[
        { value: '50', label: '50 lines' },
        { value: '100', label: '100 lines' },
        { value: '200', label: '200 lines' },
        { value: '500', label: '500 lines' },
      ]} onchange={load} />
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
    padding: 1rem; font-family: 'Consolas', 'Menlo', 'Monaco', 'DejaVu Sans Mono', 'Noto Sans Mono', 'Courier New', monospace;
    font-weight: 500; font-size: 0.78rem; line-height: 1.5; color: var(--text-secondary); max-height: 70vh;
    overflow-y: auto; white-space: pre-wrap; word-break: break-all;
    animation: fadeIn 0.3s ease-out;
  }
  @keyframes fadeIn {
    from { opacity: 0; transform: translateY(6px); }
    to { opacity: 1; transform: translateY(0); }
  }
</style>
