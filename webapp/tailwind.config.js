/** @type {import('tailwindcss').Config} */
export default {
  content: ['./index.html', './src/**/*.{ts,tsx}'],
  theme: {
    extend: {
      colors: {
        navy: {
          950: '#050b1d',
          900: '#0a1430',
          850: '#0e1a3d',
          800: '#13224d',
          700: '#1b2f66',
          600: '#284284',
        },
        ink: { DEFAULT: '#e7edff', muted: '#93a4cc', dim: '#62749f' },
        accent: { DEFAULT: '#5d9cff', soft: '#2c4f9e', glow: '#8bb9ff' },
        crema: '#f3b562',
      },
      fontFamily: {
        sans: ['Inter', 'system-ui', '-apple-system', 'Segoe UI', 'Roboto', 'sans-serif'],
      },
    },
  },
  plugins: [],
};
