# NPM Packaging Solution for Frontend

## Problem

When publishing Queen to npm, the `webapp/dist/index.html` file was not being included in the package, even though all the assets were. This was due to npm's file inclusion logic which doesn't include root-level files when specifying a directory.

## Solution

Copy the built frontend into `src/webapp-dist` during the `prepublishOnly` script. Since `src/` is already whitelisted in the `files` array, everything in `src/webapp-dist` gets included automatically.

## Implementation

### 1. Package.json Scripts

```json
{
  "scripts": {
    "build:webapp": "cd webapp && npm install && npm run build",
    "copy:webapp": "rm -rf src/webapp-dist && cp -r webapp/dist src/webapp-dist",
    "prepublishOnly": "npm run build:webapp && npm run copy:webapp"
  },
  "files": [
    "src/",
    "init-db.js",
    "LICENSE.md",
    "README.md"
  ]
}
```

### 2. Server Path Logic (src/server.js)

```javascript
// Default webapp path: try src/webapp-dist (npm package), fall back to ../webapp/dist (development)
let defaultWebappPath = path.join(__dirname, 'webapp-dist');
if (!fs.existsSync(defaultWebappPath)) {
  defaultWebappPath = path.join(__dirname, '..', 'webapp', 'dist');
}
const WEBAPP_PATH = options.webappPath || defaultWebappPath;
```

### 3. Gitignore

Add to `.gitignore`:
```
# Built webapp for npm package (copied during prepublishOnly)
src/webapp-dist
```

## How It Works

1. **Development**: Server looks for `src/webapp-dist`, doesn't find it, falls back to `../webapp/dist`
2. **Publishing**: `prepublishOnly` runs, builds webapp, copies to `src/webapp-dist`
3. **NPM Package**: Includes everything in `src/`, including `src/webapp-dist/` with all files
4. **Installed Package**: Server finds `src/webapp-dist` and serves from there

## Benefits

✅ Simple and clean solution
✅ No fighting with npm file inclusion rules  
✅ Works automatically in both development and production  
✅ `src/webapp-dist` ignored by git  
✅ Webapp built and copied automatically when publishing  
✅ Full frontend (including index.html) included in npm package

## Testing

```bash
# Build and copy webapp
npm run copy:webapp

# Create npm package
npm pack

# Verify index.html is included
tar -tzf queen-mq-*.tgz | grep 'index.html'
# Should output: package/src/webapp-dist/index.html

# Count all webapp files
tar -tzf queen-mq-*.tgz | grep 'src/webapp-dist' | wc -l
# Should output: 32 (index.html + all assets)
```

## Publishing Workflow

### Quick Publish (One Command)

```bash
# 1. Update version
npm version patch  # or minor, or major

# 2. Publish everything in one command
npm run publish:public
```

This single command will:
- Build the webapp (`npm run build:webapp`)
- Copy to src/webapp-dist (`npm run copy:webapp`)
- Publish to npm with public access (`npm publish --access public`)

### Manual Publish (Step by Step)

```bash
# 1. Make your changes

# 2. Update version in package.json
npm version patch  # or minor, or major

# 3. Build and prepare
npm run build:webapp
npm run copy:webapp

# 4. Publish
npm publish --access public

# Or just: npm publish (prepublishOnly runs automatically)
```

The `prepublishOnly` script will automatically run before any `npm publish`, ensuring the webapp is always built and copied.

## Result

Users installing `queen-mq` from npm can now use:

```javascript
import { QueenServer } from 'queen-mq';

await QueenServer({
  port: 3000,
  serveWebapp: true  // Frontend is automatically served from included files!
});
```

No manual building or copying required on the user's end!

