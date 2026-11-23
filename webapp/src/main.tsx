import { createRoot } from 'react-dom/client';
import App from './App';
import './index.css';

// web_server's page is a bare document with <esp-app>: give it a viewport, a title and our root instead.
function mount() {
  document.querySelector('esp-app')?.remove();
  if (!document.querySelector('meta[name=viewport]')) {
    const m = document.createElement('meta');
    m.name = 'viewport';
    m.content = 'width=device-width, initial-scale=1, viewport-fit=cover';
    document.head.appendChild(m);
  }
  const theme = document.createElement('meta');
  theme.name = 'theme-color';
  theme.content = '#050b1d';
  document.head.appendChild(theme);
  document.title = 'ECAM';
  const root = document.createElement('div');
  root.id = 'ecam-ui';
  document.body.appendChild(root);
  createRoot(root).render(<App />);
}

if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', mount);
else mount();
