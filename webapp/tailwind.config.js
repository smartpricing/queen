export default {
  content: [
    "./index.html",
    "./src/**/*.{vue,js}",
  ],
  darkMode: 'class',
  theme: {
    extend: {
      colors: {
        // Primary brand colors - Rose & Purple theme
        'queen-rose': '#f43f5e',         // rose-500
        'queen-purple': '#a855f7',       // purple-500
        'queen-pink': '#ec4899',         // pink-500
        // Aliases for primary/secondary
        'primary': '#f43f5e',
        'secondary': '#a855f7',
      },
      fontFamily: {
        sans: ['Inter', '-apple-system', 'BlinkMacSystemFont', 'Segoe UI', 'sans-serif'],
        mono: ['JetBrains Mono', 'Courier New', 'monospace'],
      },
    },
  },
  plugins: [],
};
