import { cubicOut, cubicInOut } from 'svelte/easing';
import { fade, fly, scale } from 'svelte/transition';

// Unica fonte di tempi/distanze: le variabili CSS vengono generate da qui.
export const motion = Object.freeze({
  fast: 120,
  enter: 200,
  layout: 260,
  exit: 150,
  stagger: 18,
  staggerMax: 144,
  spinner: 800,
  shimmer: 1200,
  distance: 8,
  panelScale: 0.98,
  easeOut: 'cubic-bezier(0.215, 0.61, 0.355, 1)',
  easeInOut: 'cubic-bezier(0.645, 0.045, 0.355, 1)',
});

export function initMotion(root = document.documentElement) {
  for (const [name, value] of Object.entries(motion)) {
    const unit = name === 'distance' ? 'px' : typeof value === 'number' && name !== 'panelScale' ? 'ms' : '';
    const key = name.replace(/[A-Z]/g, letter => '-' + letter.toLowerCase());
    root.style.setProperty('--motion-' + key, value + unit);
  }
}

export function motionDuration(kind = 'enter') {
  return typeof window !== 'undefined' && window.matchMedia?.('(prefers-reduced-motion: reduce)').matches
    ? 0 : motion[kind];
}

export function pageEnter(node) {
  return fly(node, { y: motion.distance / 2, duration: motionDuration(), easing: cubicOut });
}

export function overlayExit(node) {
  return fade(node, { duration: motionDuration('exit'), easing: cubicInOut });
}

export function contentEnter(node) {
  return fade(node, { duration: motionDuration(), easing: cubicOut });
}

export function panelExit(node) {
  const duration = motionDuration('exit');
  // Una sola trasformazione, senza sovrascrivere lo stile di base.
  const style = getComputedStyle(node);
  const transform = style.transform;
  const opacity = Number(style.opacity);
  const base = transform === 'none' ? '' : transform;
  return {
    duration, easing: cubicInOut,
    css: (t, u) => `opacity: ${opacity * t}; transform: ${base} translateY(${u * motion.distance}px) scale(${motion.panelScale + (1 - motion.panelScale) * t});`,
  };
}

export function lightboxExit(node) {
  return scale(node, { start: motion.panelScale, duration: motionDuration('exit'), easing: cubicInOut });
}

export function relayoutDuration() {
  return motionDuration('layout');
}

export const relayoutEasing = cubicOut;

// Il tap può essere più breve della transition CSS :active. Il feedback
// touch/pen completa un singolo impulso anche dopo il rilascio, senza hover
// persistenti né trasformazioni sul wrapper FLIP delle collezioni.
export function installTouchFeedback(target = document) {
  const animations = new WeakMap();
  function onPress(event) {
    if (!['touch', 'pen'].includes(event.pointerType) || event.isPrimary === false) return;
    const node = event.target?.closest?.('button, [role="button"]');
    if (!node || node.matches(':disabled, [aria-disabled="true"]') || node.closest('[inert]')) return;
    const duration = motionDuration();
    if (!duration || !node.animate) return;
    animations.get(node)?.cancel();
    const wholeContent = node.matches('.card-open, .row-open, .home-row, .detail-poster-btn');
    const filter = getComputedStyle(node).filter;
    const base = filter === 'none' ? '' : filter;
    const frames = wholeContent
      ? [{ filter: base || 'none' }, { filter: `${base} brightness(0.88)`, offset: 0.35 }, { filter: base || 'none' }]
      : [{ scale: '1' }, { scale: '0.96', offset: 0.35 }, { scale: '1' }];
    animations.set(node, node.animate(frames, { duration, easing: motion.easeOut }));
  }
  target.addEventListener('pointerdown', onPress, { passive: true });
  return () => target.removeEventListener('pointerdown', onPress);
}
