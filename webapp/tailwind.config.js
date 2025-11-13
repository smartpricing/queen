export default {
  content: [
    "./index.html",
    "./src/**/*.{vue,js}",
  ],
  darkMode: 'class',
  theme: {
    extend: {
      colors: {
        // Primary brand colors - Emerald green theme matching documentation website
        'queen-primary': '#059669',      // emerald-600 - Rich emerald
        'queen-secondary': '#0d9488',    // teal-600 - Jade accent
        'queen-emerald': '#059669',      // emerald-600
        'queen-jade': '#0d9488',         // teal-600
        // Aliases for convenience
        'primary': '#059669',
        'secondary': '#0d9488',
      },
      fontFamily: {
        sans: ['Inter', '-apple-system', 'BlinkMacSystemFont', 'Segoe UI', 'sans-serif'],
        mono: ['JetBrains Mono', 'Courier New', 'monospace'],
      },
    },
  },
  plugins: [],
};
