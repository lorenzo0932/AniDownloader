import App from './App.svelte';
import { mount } from 'svelte';
import { initMotion, installTouchFeedback } from './lib/motion.js';
import './lib/motion.css';
import './lib/actions.css';

initMotion();
installTouchFeedback();

const app = mount(App, {
  target: document.getElementById('app'),
});

export default app;
