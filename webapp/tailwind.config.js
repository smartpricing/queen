export default {
  content: [
    "./index.html",
    "./src/**/*.{vue,js}",
  ],
  darkMode: 'class',
  theme: {
    extend: {
      colors: {
        // Primary brand colors - Orange/Pink theme matching new punk rock logo
        'queen-primary': '#FF6B00',      // Vibrant orange
        'queen-secondary': '#E91E63',    // Hot pink
        'queen-orange': '#FF6B00',       // Vibrant orange
        'queen-pink': '#E91E63',         // Hot pink
        'queen-deep-pink': '#C2185B',    // Deep pink
        // Aliases for convenience
        'primary': '#FF6B00',
        'secondary': '#E91E63',
      },
      fontFamily: {
        sans: ['Inter', '-apple-system', 'BlinkMacSystemFont', 'Segoe UI', 'sans-serif'],
        mono: ['JetBrains Mono', 'Courier New', 'monospace'],
      },
    },
  },
  plugins: [],
};
