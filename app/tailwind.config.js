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
        /* Cursor-style monochrome surfaces */
        ink: {
          0: '#141415',
          1: '#1a1a1b',
          2: '#1d1d1f',
          3: '#232325',
          4: '#252527',
          5: '#2d2d30',
        },
        /* Paper aliased to ink so light-mode call sites still render dark
           until phase 7 fully removes them */
        paper: {
          0: '#141415',
          1: '#1a1a1b',
          2: '#1d1d1f',
          3: '#232325',
          4: '#2d2d30',
        },
        /* Brand tokens, semantically repurposed
             crown → primary UI accent (white)
             ember → danger / chart-out (logo pink)
             ice   → info / chart-in (logo cyan)
             warn  → warning semantic (muted amber) */
        crown: {
          50: '#ffffff',
          100: '#fafafa',
          200: '#f5f5f5',
          300: '#e8e8e8',
          400: '#ffffff',
          500: '#d4d4d4',
          600: '#9a9a9a',
          700: '#6a6a6a',
          800: '#464649',
          900: '#2d2d30',
          950: '#1a1a1b',
          glow: 'rgba(255, 255, 255, 0.08)',
        },
        ember: {
          400: '#fb7185',
          500: '#f43f5e',
          600: '#e11d48',
          glow: 'rgba(244, 63, 94, 0.10)',
        },
        ice: {
          400: '#22d3ee',
          500: '#06b6d4',
          600: '#0891b2',
          glow: 'rgba(34, 211, 238, 0.10)',
        },
        warn: {
          400: '#e6b450',
          500: '#d9a03d',
          glow: 'rgba(230, 180, 80, 0.10)',
        },
        ok: {
          500: '#4ade80',
          glow: 'rgba(74, 222, 128, 0.10)',
        },
        accent: {
          DEFAULT: '#ffffff',
          dim: '#d4d4d4',
        },
        /* Text tokens — unchanged names, new values come from CSS vars */
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
        /* Glows kept as tokens but heavily dimmed; Cursor has no neon */
        'glow-crown': '0 0 0 1px rgba(255, 255, 255, 0.08)',
        'glow-ember': '0 0 0 1px rgba(244, 63, 94, 0.15)',
        'glow-ice':   '0 0 0 1px rgba(34, 211, 238, 0.15)',
        'soft':       '0 2px 15px -3px rgba(0, 0, 0, 0.30)',
        'soft-lg':    '0 10px 40px -10px rgba(0, 0, 0, 0.45)',
      },
      animation: {
        'fade-in':    'fadeIn 0.18s ease-out both',
        'slide-up':   'slideUp 0.18s ease-out both',
        'pulse-slow': 'pulse 3s cubic-bezier(0.4, 0, 0.6, 1) infinite',
        'viewIn':     'viewIn 0.18s cubic-bezier(.2,.7,.2,1) both',
      },
      keyframes: {
        fadeIn: {
          '0%': { opacity: '0' },
          '100%': { opacity: '1' },
        },
        slideUp: {
          '0%': { opacity: '0', transform: 'translateY(3px)' },
          '100%': { opacity: '1', transform: 'translateY(0)' },
        },
        viewIn: {
          from: { opacity: '0', transform: 'translateY(3px)' },
          to: { opacity: '1', transform: 'none' },
        },
      },
    },
  },
  plugins: [],
}
