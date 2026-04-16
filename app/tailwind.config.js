/** @type {import('tailwindcss').Config} */
export default {
  content: [
    "./index.html",
    "./src/**/*.{vue,js,ts,jsx,tsx}",
  ],
  darkMode: 'class',
  theme: {
    extend: {
      colors: {
        /* Ink surfaces (dark mode) */
        ink: {
          0: '#07070a',
          1: '#0b0b0f',
          2: '#101015',
          3: '#17171e',
          4: '#1f1f28',
          5: '#2a2a35',
        },
        /* Paper surfaces (light mode) */
        paper: {
          0: '#fafaf7',
          1: '#f4f4ef',
          2: '#ecece6',
          3: '#dcdcd4',
          4: '#c5c5bc',
        },
        /* Brand — royal trio */
        crown: {
          50: '#FFFBEB',
          100: '#FEF3C7',
          200: '#FDE68A',
          300: '#FCD34D',
          400: '#fbbf24',
          500: '#f59e0b',
          600: '#d97706',
          700: '#B45309',
          800: '#92400E',
          900: '#78350F',
          950: '#451A03',
          glow: 'rgba(251, 191, 36, 0.18)',
        },
        ember: {
          400: '#fb7185',
          500: '#f43f5e',
          600: '#e11d48',
          glow: 'rgba(244, 63, 94, 0.18)',
        },
        ice: {
          400: '#22d3ee',
          500: '#06b6d4',
          600: '#0891b2',
          glow: 'rgba(34, 211, 238, 0.18)',
        },
        ok: {
          500: '#34d399',
        },
        /* Text tokens */
        'txt-hi': 'var(--text-hi)',
        'txt-mid': 'var(--text-mid)',
        'txt-low': 'var(--text-low)',
        'txt-faint': 'var(--text-faint)',
      },
      fontFamily: {
        sans: ['Inter', 'ui-sans-serif', 'system-ui', 'sans-serif'],
        mono: ['JetBrains Mono', 'ui-monospace', 'monospace'],
      },
      boxShadow: {
        'glow-crown': '0 0 20px -5px rgba(251, 191, 36, 0.4)',
        'glow-ember': '0 0 20px -5px rgba(244, 63, 94, 0.4)',
        'glow-ice': '0 0 20px -5px rgba(34, 211, 238, 0.4)',
        'soft': '0 2px 15px -3px rgba(0, 0, 0, 0.07), 0 10px 20px -2px rgba(0, 0, 0, 0.04)',
        'soft-lg': '0 10px 40px -10px rgba(0, 0, 0, 0.1), 0 2px 10px -2px rgba(0, 0, 0, 0.04)',
      },
      animation: {
        'fade-in': 'fadeIn 0.5s ease-out',
        'slide-up': 'slideUp 0.5s ease-out',
        'pulse-slow': 'pulse 3s cubic-bezier(0.4, 0, 0.6, 1) infinite',
        'viewIn': 'viewIn 0.35s cubic-bezier(.2,.7,.2,1)',
      },
      keyframes: {
        fadeIn: {
          '0%': { opacity: '0' },
          '100%': { opacity: '1' },
        },
        slideUp: {
          '0%': { opacity: '0', transform: 'translateY(4px)' },
          '100%': { opacity: '1', transform: 'translateY(0)' },
        },
        viewIn: {
          from: { opacity: '0', transform: 'translateY(4px)' },
          to: { opacity: '1', transform: 'none' },
        },
      },
    },
  },
  plugins: [],
}
