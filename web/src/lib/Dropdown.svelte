<script>
let { value = $bindable(), options = [], onchange } = $props();
let open = $state(false);

function toggle() {
  open = !open;
}

function select(val) {
  value = val;
  open = false;
  onchange?.(val);
}

function handleClickOutside() {
  open = false;
}

function handleKeydown(e) {
  if (e.key === 'Escape') open = false;
  if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); toggle(); }
}
</script>

<svelte:window onclick={handleClickOutside} />

<div class="dropdown">
  <button class="dropdown-trigger" onclick={(e) => { e.stopPropagation(); toggle(); }} onkeydown={handleKeydown}
    aria-haspopup="listbox" aria-expanded={open}>
    <span class="dropdown-label">{options.find(o => o.value === value)?.label ?? ''}</span>
    <svg class="dropdown-chevron" class:open viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><polyline points="6 9 12 15 18 9"/></svg>
  </button>
  {#if open}
    <div class="dropdown-menu" role="listbox">
      {#each options as opt}
        <button class="dropdown-item" class:active={opt.value === value} role="option" aria-selected={opt.value === value}
          onclick={(e) => { e.stopPropagation(); select(opt.value); }}>
          {opt.label}
        </button>
      {/each}
    </div>
  {/if}
</div>

<style>
  .dropdown {
    position: relative;
    display: inline-block;
  }
  .dropdown-trigger {
    display: flex;
    align-items: center;
    gap: 0.4rem;
    background: var(--bg-secondary);
    border: 1px solid var(--border-color);
    border-radius: 6px;
    color: var(--text-primary);
    padding: 0.35rem 0.5rem;
    font-size: 0.8rem;
    cursor: pointer;
    white-space: nowrap;
    font-family: inherit;
    line-height: 1.4;
  }
  .dropdown-trigger:hover {
    border-color: var(--accent);
  }
  .dropdown-label {
    flex: 1;
  }
  .dropdown-chevron {
    width: 14px;
    height: 14px;
    transition: transform 0.15s;
  }
  .dropdown-chevron.open {
    transform: rotate(180deg);
  }
  .dropdown-menu {
    position: absolute;
    top: 100%;
    left: 0;
    margin-top: 4px;
    background: var(--bg-secondary);
    border: 1px solid var(--border-color);
    border-radius: 6px;
    overflow: hidden;
    z-index: 100;
    min-width: 100%;
    box-shadow: 0 4px 12px rgba(0,0,0,0.25);
  }
  .dropdown-item {
    display: block;
    width: 100%;
    padding: 0.4rem 0.75rem;
    background: none;
    border: none;
    color: var(--text-primary);
    font-size: 0.8rem;
    text-align: left;
    cursor: pointer;
    white-space: nowrap;
    font-family: inherit;
  }
  .dropdown-item:hover, .dropdown-item.active {
    background: var(--accent);
    color: #fff;
  }
</style>
