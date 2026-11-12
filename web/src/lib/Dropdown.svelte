<script>
  // Combobox in-page: WebKitGTK rende il popup dei <select> nativi come
  // widget GTK e ignora il tema dell'app (testo bianco su fondo bianco).
  // Questo componente usa solo DOM/CSS, resta leggibile in light/dark e
  // supporta tastiera (frecce, Home/End, Enter/Spazio, Esc).
  let {
    value = $bindable(),
    options = [],
    onchange,
    label = '',
    disabled = false,
    id = 'dropdown',
  } = $props();

  let open = $state(false);
  let active = $state(-1);
  let trigger = $state(null);

  let selectedIndex = $derived(options.findIndex((o) => o.value === value));
  let selectedLabel = $derived(options.find((o) => o.value === value)?.label ?? '');

  function apri() {
    if (disabled) return;
    open = true;
    active = selectedIndex >= 0 ? selectedIndex : 0;
  }

  function chiudi() {
    open = false;
    active = -1;
  }

  function toggle() {
    if (open) chiudi();
    else apri();
  }

  function scegli(index) {
    const opt = options[index];
    if (!opt) return;
    value = opt.value;
    chiudi();
    onchange?.(opt.value);
    trigger?.focus();
  }

  function sposta(delta) {
    if (!options.length) return;
    if (!open) {
      apri();
      return;
    }
    active = (active + delta + options.length) % options.length;
  }

  function onKeydown(e) {
    if (disabled) return;
    switch (e.key) {
      case 'ArrowDown':
        e.preventDefault();
        sposta(1);
        break;
      case 'ArrowUp':
        e.preventDefault();
        sposta(-1);
        break;
      case 'Home':
        if (open) {
          e.preventDefault();
          active = 0;
        }
        break;
      case 'End':
        if (open) {
          e.preventDefault();
          active = options.length - 1;
        }
        break;
      case 'Enter':
      case ' ':
        e.preventDefault();
        if (open) scegli(active);
        else apri();
        break;
      case 'Escape':
        if (open) {
          e.preventDefault();
          chiudi();
        }
        break;
      case 'Tab':
        chiudi();
        break;
    }
  }

  function optionId(index) {
    return `${id}-opt-${index}`;
  }
</script>

<svelte:window
  onclick={chiudi}
  onkeydown={(e) => {
    if (e.key === 'Escape' && open) chiudi();
  }}
/>

<div class="dropdown" class:disabled>
  <button
    {id}
    bind:this={trigger}
    type="button"
    class="dropdown-trigger"
    role="combobox"
    aria-haspopup="listbox"
    aria-expanded={open}
    aria-controls={open ? `${id}-listbox` : undefined}
    aria-label={label || undefined}
    aria-activedescendant={open && active >= 0 ? optionId(active) : undefined}
    aria-disabled={disabled}
    {disabled}
    onclick={(e) => {
      e.stopPropagation();
      toggle();
    }}
    onkeydown={onKeydown}
  >
    <span class="dropdown-label">{selectedLabel}</span>
    <svg class="dropdown-chevron" class:open viewBox="0 0 24 24" fill="none"
         stroke="currentColor" stroke-width="2" aria-hidden="true">
      <polyline points="6 9 12 15 18 9" />
    </svg>
  </button>

  {#if open}
    <div class="dropdown-menu" role="listbox" id={`${id}-listbox`}
         aria-label={label || undefined}>
      {#each options as opt, i (opt.value)}
        <button
          type="button"
          class="dropdown-item"
          class:active={opt.value === value}
          class:highlighted={i === active}
          id={optionId(i)}
          role="option"
          aria-selected={opt.value === value}
          tabindex="-1"
          onmouseenter={() => (active = i)}
          onclick={(e) => {
            e.stopPropagation();
            scegli(i);
          }}
        >
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
    min-width: 0;
  }
  .dropdown.disabled { opacity: 0.6; }
  .dropdown-trigger {
    display: flex;
    align-items: center;
    gap: 0.4rem;
    min-height: 36px;
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
  .dropdown-trigger:hover:not(:disabled) { border-color: var(--accent); }
  .dropdown-trigger:focus-visible {
    outline: 2px solid var(--accent);
    outline-offset: 1px;
  }
  .dropdown-trigger:disabled { cursor: default; }
  .dropdown-label { flex: 1; text-align: left; }
  .dropdown-chevron {
    width: 14px;
    height: 14px;
    flex: 0 0 auto;
    transition: transform var(--motion-fast) var(--motion-ease-out);
  }
  .dropdown-chevron.open { transform: rotate(180deg); }
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
    max-height: 280px;
    overflow-y: auto;
    box-shadow: 0 4px 12px rgba(0, 0, 0, 0.25);
    animation: motion-rise var(--motion-enter) var(--motion-ease-out);
  }
  .dropdown-item {
    display: block;
    width: 100%;
    padding: 0.45rem 0.75rem;
    background: none;
    border: none;
    color: var(--text-primary);
    font-size: 0.8rem;
    text-align: left;
    cursor: pointer;
    white-space: nowrap;
    font-family: inherit;
  }
  .dropdown-item.highlighted,
  .dropdown-item:hover,
  .dropdown-item.active {
    background: var(--accent);
    color: #fff;
  }
</style>
