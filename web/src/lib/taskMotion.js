import { motion, motionDuration } from './motion.js';

const running = new WeakMap();
const visible = r => r.width > 0 && r.height > 0 && r.top >= 0 && r.left >= 0 &&
  r.bottom <= window.innerHeight && r.right <= window.innerWidth;

export function captureTaskRects(root) {
  return new Map(Array.from(root?.querySelectorAll('[data-task-key]') || [], node =>
    [node.dataset.taskKey, node.getBoundingClientRect()]));
}

export function moveTasks(root, previous) {
  const duration = motionDuration('layout');
  if (!duration || !previous.size) return;
  for (const node of root?.querySelectorAll('[data-task-key]') || []) {
    const from = previous.get(node.dataset.taskKey);
    const to = node.getBoundingClientRect();
    if (!node.animate || !visible(to)) continue;
    const dx = from ? from.left - to.left : 0;
    const dy = from ? from.top - to.top : 0;
    if (from && visible(from) && (Math.abs(dx) > 1 || Math.abs(dy) > 1)) {
      running.get(node)?.cancel();
      running.set(node, node.animate([
        { transform: `translate(${dx}px, ${dy}px)`, opacity: 0.8 },
        { transform: 'translate(0, 0)', opacity: 1 },
      ], { duration, easing: motion.easeOut }));
      const animation = running.get(node);
      node.style.zIndex = '2';
      const finish = () => { if (running.get(node) === animation) node.style.zIndex = ''; };
      animation.finished?.then(finish, finish);
    } else if (!from || !visible(from)) {
      running.get(node)?.cancel();
      running.set(node, node.animate([{ opacity: 0 }, { opacity: 1 }],
        { duration: motionDuration('enter'), easing: motion.easeOut }));
    }
  }
}
