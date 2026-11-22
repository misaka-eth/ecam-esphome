import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';
import cssInjectedByJs from 'vite-plugin-css-injected-by-js';

// `npm run build` writes one self-contained script (CSS inlined) to components/ecam_web/ecam.js, which
// the ecam_web component embeds in the firmware. `ESP=http://<ip> npm run dev` serves the UI locally
// and proxies the ESP's web_server API.
const esp = process.env.ESP ?? 'http://coffee.local';
const api = '^/(events|sensor|binary_sensor|text_sensor|select|number|switch|button|text)(/|$)';

export default defineConfig({
  plugins: [react(), cssInjectedByJs()],
  define: { 'process.env.NODE_ENV': JSON.stringify('production') },
  server: {
    proxy: {
      [api]: {
        target: esp,
        changeOrigin: true,
        // web_server rejects cross-origin requests; to the ESP these come from the proxy itself.
        configure: (proxy) => proxy.on('proxyReq', (req) => req.removeHeader('origin')),
      },
    },
  },
  build: {
    outDir: '../components/ecam_web',
    emptyOutDir: false,
    target: 'es2019',
    cssCodeSplit: false,
    minify: 'esbuild',
    lib: { entry: 'src/main.tsx', formats: ['iife'], name: 'EcamUI', fileName: () => 'ecam.js' },
  },
});
