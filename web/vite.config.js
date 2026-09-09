import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'
import { sentryVitePlugin } from '@sentry/vite-plugin'
import { swellPagePlugin } from './scripts/swell-page.mjs'

export default defineConfig(({ mode }) => {
  const sentryUploadEnabled = mode === 'production' && Boolean(
    process.env.VITE_SENTRY_DSN && process.env.SENTRY_AUTH_TOKEN && process.env.SENTRY_ORG &&
    process.env.SENTRY_PROJECT && process.env.VITE_SENTRY_RELEASE,
  )

  return {
    base: './',
    plugins: [
      vue(),
      swellPagePlugin(),
      ...(sentryUploadEnabled ? [sentryVitePlugin({
        org: process.env.SENTRY_ORG,
        project: process.env.SENTRY_PROJECT,
        authToken: process.env.SENTRY_AUTH_TOKEN,
        telemetry: false,
        release: {
          name: process.env.VITE_SENTRY_RELEASE,
          inject: false,
        },
        sourcemaps: {
          assets: './dist/assets/**',
          filesToDeleteAfterUpload: './dist/**/*.map',
        },
      })] : []),
    ],
    server: {
      port: 4174,
      strictPort: true,
    },
    build: {
      chunkSizeWarningLimit: 700,
      sourcemap: sentryUploadEnabled ? 'hidden' : false,
    },
    test: {
      environment: 'happy-dom',
      include: ['tests/**/*.test.js'],
    },
  }
})
