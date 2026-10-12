import App from './App.svelte';
import { mount } from 'svelte';
import { initMotion } from './lib/motion.js';
import './lib/motion.css';

initMotion();

const app = mount(App, {
  target: document.getElementById('app'),
});

export default app;
