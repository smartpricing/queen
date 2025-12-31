import { defineConfig } from 'vitepress'
import { readFileSync } from 'fs'
import { fileURLToPath } from 'url'
import { dirname, join } from 'path'

// Read version from server/server.json (single source of truth)
const __dirname = dirname(fileURLToPath(import.meta.url))
const serverJsonPath = join(__dirname, '../../server/server.json')
const serverJson = JSON.parse(readFileSync(serverJsonPath, 'utf-8'))
const QUEEN_VERSION = serverJson.version

export default defineConfig({
  title: 'Queen MQ',
  description: 'Modern PostgreSQL-backed Message Queue System',
  base: '/queen/',
  head: [
    ['link', { rel: 'icon', type: 'image/png', href: '/queen/queen_head.png' }],
    ['meta', { name: 'theme-color', content: '#ec4899' }],
    ['meta', { property: 'og:type', content: 'website' }],
    ['meta', { property: 'og:title', content: 'Queen MQ - PostgreSQL-backed Message Queue' }],
    ['meta', { property: 'og:description', content: 'High-performance, feature-rich message queue system built on PostgreSQL' }],
  ],
  
  themeConfig: {
    logo: '/queen_head.png',
    
    nav: [
      { text: 'Home', link: '/' },
      { text: 'Guide', link: '/guide/introduction' },
      { 
        text: 'Clients',
        items: [
          { text: 'JavaScript Client', link: '/clients/javascript' },
          { text: 'Python Client', link: '/clients/python' },
          { text: 'C++ Client', link: '/clients/cpp' },
          { text: 'HTTP API', link: '/api/http' }
        ]
      },
      {
        text: 'Server',
        items: [
          { text: 'Architecture', link: '/server/architecture' },
          { text: 'Installation', link: '/server/installation' },
          { text: 'Deployment', link: '/server/deployment' },
          { text: 'Production Benchmarks', link: '/server/benchmark-results' }
        ]
      },
      {
        text: 'More',
        items: [
          { text: 'Web Dashboard', link: '/webapp/overview' },
          { text: 'Proxy Server', link: '/proxy/overview' },
          { text: 'PostgreSQL Extension', link: '/pg_qpubsub/' }
        ]
      }
    ],

    sidebar: {
      '/guide/': [
        {
          text: 'Getting Started',
          items: [
            { text: 'Introduction', link: '/guide/introduction' },
            { text: 'Quick Start', link: '/guide/quickstart' },
            { text: 'Installation', link: '/guide/installation' },
            { text: 'Basic Concepts', link: '/guide/concepts' },
            { text: 'Comparison', link: '/guide/comparison' }
          ]
        },
        {
          text: 'Core Features',
          items: [
            { text: 'Queues & Partitions', link: '/guide/queues-partitions' },
            { text: 'Consumer Groups', link: '/guide/consumer-groups' },
            { text: 'Transactions', link: '/guide/transactions' },
            { text: 'Long Polling', link: '/guide/long-polling' },
            { text: 'Dead Letter Queue', link: '/guide/dlq' }
          ]
        },
        {
          text: 'Advanced',
          items: [
            { text: 'Lease Management', link: '/guide/lease-management' },
            { text: 'Message Retention', link: '/guide/retention' },
            { text: 'Failover & Recovery', link: '/guide/failover' },
            { text: 'Message Tracing', link: '/guide/tracing' },
            { text: 'Maintenance Operations', link: '/guide/maintenance-operations' }
          ]
        }
      ],
      
      '/clients/': [
        {
          text: 'Client Libraries',
          items: [
            { text: 'JavaScript Client', link: '/clients/javascript' },
            { text: 'Python Client', link: '/clients/python' },
            { text: 'C++ Client', link: '/clients/cpp' }
          ]
        },
        {
          text: 'Examples',
          items: [
            { text: 'Basic Usage', link: '/clients/examples/basic' },
            { text: 'Python Examples', link: '/clients/examples/python' },
            { text: 'Batch Operations', link: '/clients/examples/batch' },
            { text: 'Transactions', link: '/clients/examples/transactions' },
            { text: 'Consumer Groups', link: '/clients/examples/consumer-groups' }
          ]
        }
      ],
      
      '/server/': [
        {
          text: 'Server',
          items: [
            { text: 'Architecture', link: '/server/architecture' },
            { text: 'Installation', link: '/server/installation' },
            { text: 'Deployment', link: '/server/deployment' }
          ]
        },
      {
        text: 'Operations',
        items: [

          { text: 'Production Benchmark Results', link: '/server/benchmark-results' },
          { text: 'Release History', link: '/server/releases' }
        ]
      }
      ],
      
      '/webapp/': [
        {
          text: 'Web Dashboard',
          items: [
            { text: 'Overview', link: '/webapp/overview' },
            { text: 'Setup', link: '/webapp/setup' },
            { text: 'Features', link: '/webapp/features' }
          ]
        }
      ],
      
      '/proxy/': [
        {
          text: 'Proxy Server',
          items: [
            { text: 'Overview', link: '/proxy/overview' },
            { text: 'Setup & Configuration', link: '/proxy/setup' }
          ]
        }
      ],
      
      '/api/': [
        {
          text: 'API Reference',
          items: [
            { text: 'HTTP API', link: '/api/http' }
          ]
        }
      ],
      
      '/pg_qpubsub/': [
        {
          text: 'PostgreSQL Extension',
          items: [
            { text: 'Overview', link: '/pg_qpubsub/' }
          ]
        }
      ]
    },

    socialLinks: [
      { icon: 'github', link: 'https://github.com/smartpricing/queen' },
      { icon: 'linkedin', link: 'https://www.linkedin.com/company/smartness-com/' }
    ],

    footer: {
      message: 'Built with ❤️ by <a href="https://www.linkedin.com/company/smartness-com/" target="_blank">Smartness</a>',
      copyright: 'Copyright © 2025 Smartness. Released under the Apache 2.0 License.'
    },

    search: {
      provider: 'local'
    },

    editLink: {
      pattern: 'https://github.com/smartpricing/queen/edit/master/website/:path',
      text: 'Edit this page on GitHub'
    },

    outline: {
      level: [2, 3],
      label: 'On this page'
    }
  },

  markdown: {
    theme: {
      light: 'github-light',
      dark: 'github-dark'
    },
    lineNumbers: true,
    config: (md) => {
      // Replace {{VERSION}} placeholder with actual version from server.json
      const defaultRender = md.render.bind(md)
      md.render = function (src, env) {
        const srcWithVersion = src.replace(/\{\{VERSION\}\}/g, QUEEN_VERSION)
        return defaultRender(srcWithVersion, env)
      }
    }
  }
})
