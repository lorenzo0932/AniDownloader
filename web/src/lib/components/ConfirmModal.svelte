<script>
  let { show = false, title = 'Conferma', message = '', confirmText = 'Conferma', cancelText = 'Annulla', danger = false, onConfirm, onCancel } = $props();

  let confirmId = $state('');

  function handleKeydown(e) {
    if (e.key === 'Escape' && onCancel) onCancel();
    if (e.key === 'Enter' && onConfirm) onConfirm();
  }
</script>

{#if show}
  <div class="confirm-overlay" onclick={onCancel} onkeydown={handleKeydown} role="dialog" aria-modal="true" tabindex="-1">
    <div class="confirm-modal" onclick={(e) => e.stopPropagation()} onkeydown={(e) => e.stopPropagation()} role="presentation">
      <div class="confirm-header">
        <h3>{title}</h3>
      </div>
      <div class="confirm-body">
        <p>{message}</p>
      </div>
      <div class="confirm-footer">
        <button class="btn-cancel" onclick={onCancel}>{cancelText}</button>
        <button class="btn-confirm" class:danger={danger} onclick={onConfirm}>{confirmText}</button>
      </div>
    </div>
  </div>
{/if}

<style>
  .confirm-overlay {
    position: fixed; top: 0; left: 0; right: 0; bottom: 0;
    background: var(--overlay); z-index: 300;
    display: flex; align-items: center; justify-content: center;
    animation: fadeIn 0.15s ease;
    border: none; padding: 0; cursor: default;
  }
  .confirm-modal {
    background: var(--bg-secondary); border: 1px solid var(--border-color);
    border-radius: 12px; padding: 1.25rem; min-width: 320px; max-width: 90vw;
    animation: scaleIn 0.2s ease-out;
    box-shadow: 0 12px 40px rgba(0,0,0,0.5);
  }
  .confirm-header { margin-bottom: 0.75rem; }
  .confirm-header h3 { font-size: 1rem; font-weight: 700; color: var(--text-primary); }
  .confirm-body { margin-bottom: 1.25rem; }
  .confirm-body p { font-size: 0.85rem; color: var(--text-secondary); line-height: 1.5; white-space: pre-wrap; }
  .confirm-footer { display: flex; gap: 0.5rem; justify-content: flex-end; }
  .btn-cancel {
    padding: 0.55rem 1.1rem; background: none; border: 1px solid var(--btn-cancel-border);
    border-radius: 8px; color: var(--text-secondary); font-size: 0.85rem; cursor: pointer;
  }
  .btn-cancel:hover { background: var(--bg-tertiary); color: var(--text-primary); }
  .btn-confirm {
    padding: 0.55rem 1.1rem; background: var(--accent); border: none; border-radius: 8px;
    color: #fff; font-size: 0.85rem; font-weight: 600; cursor: pointer;
  }
  .btn-confirm:hover { background: var(--accent-hover); }
  .btn-confirm.danger { background: var(--danger); }
  .btn-confirm.danger:hover { background: #ef4444; }

  @keyframes fadeIn { from { opacity: 0; } to { opacity: 1; } }
  @keyframes scaleIn { from { opacity: 0; transform: scale(0.95); } to { opacity: 1; transform: scale(1); } }
</style>