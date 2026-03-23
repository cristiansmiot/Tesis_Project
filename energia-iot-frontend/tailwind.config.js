/** @type {import('tailwindcss').Config} */
export default {
  content: [
    "./index.html",
    "./src/**/*.{js,ts,jsx,tsx}",
  ],
  theme: {
    extend: {
      colors: {
        primary: {
          50: '#eff6ff',
          100: '#dbeafe',
          500: '#3b82f6',
          600: '#2563eb',
          700: '#1d4ed8',
        },
        energia: {
          verde: '#10b981',
          amarillo: '#f59e0b',
          rojo: '#ef4444',
          azul: '#3b82f6',
        }
      }
    },
  },
  plugins: [],
}
