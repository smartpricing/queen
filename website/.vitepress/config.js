import { defineConfig } from 'vitepress'

export default defineConfig({
  title: 'Queen MQ',
  description: 'Modern PostgreSQL-backed Message Queue System',
  base: '/',
  head: [
    ['link', { rel: 'icon', type: 'image/svg+xml', href: '/queen-logo.svg' }],
    ['meta', { name: 'theme-color', content: '#3b82f6' }],
    ['meta', { property: 'og:type', content: 'website' }],
    ['meta', { property: 'og:title', content: 'Queen MQ - PostgreSQL-backed Message Queue' }],
    ['meta', { property: 'og:description', content: 'High-performance, feature-rich message queue system built on PostgreSQL' }],
  ],
  
  themeConfig: {
    logo: '/queen-logo.svg',
    
    nav: [
      { text: 'Home', link: '/' },
      { text: 'Quick Start', link: '/guide/quickstart' },
      { text: 'Guide', link: '/guide/introduction' },
      { 
        text: 'Clients',
        items: [
          { text: 'JavaScript Client', link: '/clients/javascript' },
          { text: 'C++ Client', link: '/clients/cpp' },
          { text: 'HTTP API', link: '/api/http' }
        ]
      },
      {
        text: 'Resources',
        items: [
          { text: 'GitHub', link: 'https://github.com/smartpricing/queen' },
          { text: 'LinkedIn', link: 'https://www.linkedin.com/company/smartness-com/' },
          { text: 'Docker Hub', link: 'https://hub.docker.com/r/smartnessai/queen-mq' }
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
            { text: 'Basic Concepts', link: '/guide/concepts' }
          ]
        },
        {
          text: 'Core Features',
          items: [
            { text: 'Queues & Partitions', link: '/guide/queues-partitions' },
            { text: 'Consumer Groups', link: '/guide/consumer-groups' },
            { text: 'Transactions', link: '/guide/transactions' },
            { text: 'Long Polling', link: '/guide/long-polling' },
            { text: 'Streaming', link: '/guide/streaming' },
            { text: 'Dead Letter Queue', link: '/guide/dlq' }
          ]
        },
        {
          text: 'Advanced',
          items: [
            { text: 'Lease Management', link: '/guide/lease-management' },
            { text: 'Message Retention', link: '/guide/retention' },
            { text: 'Failover & Recovery', link: '/guide/failover' },
            { text: 'Message Tracing', link: '/guide/tracing' }
          ]
        }
      ],
      
      '/clients/': [
        {
          text: 'Client Libraries',
          items: [
            { text: 'JavaScript Client', link: '/clients/javascript' },
            { text: 'C++ Client', link: '/clients/cpp' }
          ]
        },
        {
          text: 'Examples',
          items: [
            { text: 'Basic Usage', link: '/clients/examples/basic' },
            { text: 'Batch Operations', link: '/clients/examples/batch' },
            { text: 'Transactions', link: '/clients/examples/transactions' },
            { text: 'Consumer Groups', link: '/clients/examples/consumer-groups' },
            { text: 'Streaming', link: '/clients/examples/streaming' }
          ]
        }
      ],
      
      '/server/': [
        {
          text: 'Server',
          items: [
            { text: 'Architecture', link: '/server/architecture' },
            { text: 'Installation', link: '/server/installation' },
            { text: 'Configuration', link: '/server/configuration' },
            { text: 'Performance Tuning', link: '/server/tuning' },
            { text: 'Deployment', link: '/server/deployment' }
          ]
        },
        {
          text: 'Operations',
          items: [
            { text: 'Monitoring', link: '/server/monitoring' },
            { text: 'Benchmarks', link: '/server/benchmarks' },
            { text: 'Troubleshooting', link: '/server/troubleshooting' }
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
            { text: 'HTTP API', link: '/api/http' },
            { text: 'WebSocket API', link: '/api/websocket' }
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
    lineNumbers: true
  }
})

