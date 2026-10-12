import { describe, it, expect, vi, afterEach } from 'vitest';
import {
  motion, initMotion, motionDuration, pageEnter, overlayExit,
  panelExit, lightboxExit, relayoutDuration, contentEnter, installTouchFeedback,
} from './motion.js';

afterEach(() => {
  vi.unstubAllGlobals();
  document.body.replaceChildren();
});

describe('motion standard', () => {
  it('genera i token CSS dalla stessa fonte dei tempi Svelte', () => {
    const root = document.createElement('div');
    initMotion(root);
    expect(root.style.getPropertyValue('--motion-enter')).toBe(`${motion.enter}ms`);
    expect(root.style.getPropertyValue('--motion-layout')).toBe(`${motion.layout}ms`);
    expect(root.style.getPropertyValue('--motion-distance')).toBe(`${motion.distance}px`);
    expect(root.style.getPropertyValue('--motion-panel-scale')).toBe(String(motion.panelScale));
    expect(root.style.getPropertyValue('--motion-stagger-max')).toBe(`${motion.staggerMax}ms`);
    expect(root.style.getPropertyValue('--motion-ease-out')).toBe(motion.easeOut);
  });

  it('usa i tempi condivisi anche senza matchMedia', () => {
    vi.stubGlobal('window', {});
    expect(motionDuration()).toBe(motion.enter);
    expect(relayoutDuration()).toBe(motion.layout);
  });

  it('rilegge la preferenza: il movimento ridotto può cambiare a runtime', () => {
    let reduced = false;
    vi.stubGlobal('window', { matchMedia: () => ({ matches: reduced }) });
    expect(relayoutDuration()).toBe(motion.layout);
    reduced = true;
    expect(relayoutDuration()).toBe(0);
    expect(motionDuration('exit')).toBe(0);
    reduced = false;
    expect(motionDuration('exit')).toBe(motion.exit);
  });

  it.each([
    ['pageEnter', pageEnter], ['overlayExit', overlayExit],
    ['panelExit', panelExit], ['lightboxExit', lightboxExit],
    ['contentEnter', contentEnter],
  ])(
    '%s rispetta movimento ridotto', (_name, transition) => {
      vi.stubGlobal('window', { matchMedia: () => ({ matches: true }) });
      const node = document.createElement('div');
      node.style.opacity = '1';
      expect(transition(node).duration).toBe(0);
    },
  );

  it('il pannello conserva trasformazione/opacità iniziali senza rileggere ogni frame', () => {
    const node = document.createElement('div');
    node.style.opacity = '0.8';
    node.style.transform = 'translateX(10px)';
    document.body.append(node);
    const transition = panelExit(node);
    expect(transition.duration).toBe(motion.exit);
    expect(transition.css(1, 0)).toContain('opacity: 0.8');
    expect(transition.css(0.5, 0.5)).toContain('opacity: 0.4');
    expect(transition.css(0.5, 0.5)).toContain('translateX(10px)');
    node.style.opacity = '0.1';
    expect(transition.css(0.5, 0.5)).toContain('opacity: 0.4');
    expect(transition.css(0, 1)).toContain('opacity: 0');
  });
});

describe('feedback touch condiviso', () => {
  function press(node, pointerType = 'touch') {
    node.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true, pointerType, isPrimary: true }));
  }

  it.each(['card-open', 'row-open', 'home-row', 'detail-poster-btn'])(
    '%s: impulso luminoso, senza scalare tutto il contenuto', className => {
      const node = document.createElement(className === 'home-row' ? 'div' : 'button');
      node.className = className;
      node.setAttribute('role', 'button');
      document.body.append(node);
      node.animate = vi.fn(() => ({ cancel: vi.fn() }));
      const dispose = installTouchFeedback();
      try {
        press(node);
        const [frames, options] = node.animate.mock.calls[0];
        expect(frames[1].filter).toContain('brightness');
        expect(frames[1].scale).toBeUndefined();
        expect(options.duration).toBe(motion.enter);
      } finally { dispose(); }
    },
  );

  it('pulsante piccolo: pressione visibile e cancellazione del feedback precedente', () => {
    const node = document.createElement('button');
    document.body.append(node);
    const cancel = vi.fn();
    node.animate = vi.fn(() => ({ cancel }));
    const dispose = installTouchFeedback();
    try {
      press(node);
      press(node, 'pen');
      expect(node.animate.mock.calls[0][0][1].scale).toBe('0.96');
      expect(cancel).toHaveBeenCalledOnce();
      dispose();
      press(node);
      expect(node.animate).toHaveBeenCalledTimes(2);
    } finally { dispose(); }
  });

  it('non anima mouse, controlli disabilitati o movimento ridotto', () => {
    const node = document.createElement('button');
    document.body.append(node);
    node.animate = vi.fn();
    const dispose = installTouchFeedback();
    try {
      press(node, 'mouse');
      node.disabled = true;
      press(node);
      node.disabled = false;
      node.setAttribute('aria-disabled', 'true');
      press(node);
      node.removeAttribute('aria-disabled');
      vi.stubGlobal('window', { matchMedia: () => ({ matches: true }) });
      press(node);
      expect(node.animate).not.toHaveBeenCalled();
    } finally { dispose(); }
  });
});
