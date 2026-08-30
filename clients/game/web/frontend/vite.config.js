import {defineConfig} from 'vite';

export default defineConfig({
  base: '/react/',
  build: {
    emptyOutDir: true,
    rollupOptions: {
      input: 'src/main.jsx',
      output: {
        entryFileNames: 'basilisk-react.js',
        assetFileNames: ({name}) => name?.endsWith('.css')
          ? 'basilisk-react.css'
          : 'assets/[name][extname]',
        format: 'iife',
        name: 'BasiliskReactUi',
      },
    },
  },
});
