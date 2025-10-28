export default {
  content: [
    "./index.html",
    "./src/**/*.{vue,js}",
  ],
  darkMode: 'class',
  theme: {
    extend: {
      colors: {
        // Primary brand colors - Professional Blue & Indigo theme
        'queen-blue': '#3b82f6',         // blue-500
        'queen-indigo': '#6366f1',       // indigo-500
        'queen-sky': '#0ea5e9',          // sky-500
        // Aliases for primary/secondary
        'primary': '#3b82f6',
        'secondary': '#6366f1',
      },
      fontFamily: {
        sans: ['Inter', '-apple-system', 'BlinkMacSystemFont', 'Segoe UI', 'sans-serif'],
        mono: ['JetBrains Mono', 'Courier New', 'monospace'],
      },
    },
  },
  plugins: [],
};
