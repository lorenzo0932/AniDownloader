import { describe, it, expect, vi, afterEach } from 'vitest';
import { captureTaskRects, moveTasks } from './taskMotion.js';

afterEach(() => { vi.unstubAllGlobals(); document.body.innerHTML = ''; });
const rect = (left, top) => ({ left, top, right: left + 100, bottom: top + 40, width: 100, height: 40 });
function nodeAt(position) {
  const node = document.createElement('div');
  node.dataset.taskKey = 'episode';
  node.getBoundingClientRect = () => position;
  node.animate = vi.fn(() => ({ cancel() {}, finished: Promise.resolve() }));
  document.body.append(node);
  return node;
}

describe('Movimento card fra pannelli', () => {
  it('anima la posizione con la stessa identità anche se il nodo cambia', () => {
    const old = nodeAt(rect(10, 20));
    const before = captureTaskRects(document.body);
    old.remove();
    const next = nodeAt(rect(220, 80));
    moveTasks(document.body, before);
    expect(next.animate.mock.calls[0][0][0].transform).toBe('translate(-210px, -60px)');
  });
  it('nessun movimento al primo caricamento o con movimento ridotto', () => {
    const node = nodeAt(rect(10, 20));
    moveTasks(document.body, new Map());
    expect(node.animate).not.toHaveBeenCalled();
    vi.stubGlobal('matchMedia', () => ({ matches: true }));
    moveTasks(document.body, new Map([['episode', rect(220, 80)]]));
    expect(node.animate).not.toHaveBeenCalled();
  });
  it('nessun volo da fuori viewport: solo dissolvenza', () => {
    const node = nodeAt(rect(10, 20));
    moveTasks(document.body, new Map([['episode', rect(10, -400)]]));
    expect(node.animate.mock.calls[0][0][0]).toEqual({ opacity: 0 });
  });
});
