# Webapp Setup

The web dashboard is bundled with the Queen server by default.

## Access Dashboard

Simply open your browser:

```
http://localhost:6632
```

## Development Mode

To run the webapp separately for development:

```bash
cd webapp
npm install
npm run dev
```

Dashboard available at `http://localhost:4000`.

## Configuration

Configure API endpoint in `src/api/config.js`:

```javascript
export const API_URL = 'http://localhost:6632'
```

## Build for Production

```bash
npm run build
```

Output in `dist/` folder.

## Deploy Separately

If deploying webapp separately from server:

1. Build webapp: `npm run build`
2. Serve `dist/` folder with nginx/apache
3. Configure CORS on Queen server
4. Set API_URL to Queen server address

[Complete guide](https://github.com/smartpricing/queen/tree/master/webapp)
