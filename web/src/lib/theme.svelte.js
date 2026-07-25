let theme = $state('system');
let resolved = $state('dark');
let mql = null;

function applyTheme(t) {
  const effective = t === 'system'
    ? (window.matchMedia('(prefers-color-scheme: light)').matches ? 'light' : 'dark')
    : t;
  resolved = effective;
  document.documentElement.dataset.theme = effective;
}

function getInitialTheme() {
  const saved = localStorage.getItem('theme');
  if (saved === 'light' || saved === 'dark' || saved === 'system') return saved;
  return 'system';
}

export function initTheme() {
  theme = getInitialTheme();
  applyTheme(theme);

  mql = window.matchMedia('(prefers-color-scheme: light)');
  mql.addEventListener('change', () => {
    if (theme === 'system') applyTheme('system');
  });
}

export function getTheme() { return theme; }
export function getResolvedTheme() { return resolved; }

export function setTheme(t) {
  theme = t;
  localStorage.setItem('theme', t);
  applyTheme(t);
}
