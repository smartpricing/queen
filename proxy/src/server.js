import express from 'express';
import cookieParser from 'cookie-parser';
import { createProxyMiddleware } from 'http-proxy-middleware';
import { authenticateUser, generateToken, verifyToken, verifyExternalToken, isExternalAuthEnabled } from './auth.js';
import { requireAuth, checkMethodAccess } from './middleware.js';
import { initDatabase } from './db.js';
import path from 'path';
import { fileURLToPath } from 'url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

const app = express();
const PORT = process.env.PORT || 3000;
const QUEEN_SERVER_URL = process.env.QUEEN_SERVER_URL || 'http://localhost:8080';

app.use(express.json());
app.use(cookieParser());

// Serve static files from public directory
app.use(express.static(path.join(__dirname, '..', 'public')));

// Health check
app.get('/health', (req, res) => {
  res.json({ status: 'ok' });
});

app.get('/health/ready', (req, res) => {
  res.json({ status: 'ok' });
});

// Login page - serve only if not authenticated
app.get('/login', (req, res) => {
  res.sendFile(path.join(__dirname, '..', 'public', 'login.html'));
});

// Login API endpoint
app.post('/api/login', async (req, res) => {
  const { username, password } = req.body;


  if (!username || !password) {
    return res.status(400).json({ error: 'Username and password required' });
  }

  try {
    const user = await authenticateUser(username, password);

    if (!user) {
      return res.status(401).json({ error: 'Invalid credentials' });
    }

    const token = generateToken(user);

    // Set HTTP-only cookie for security
    res.cookie('token', token, {
      httpOnly: true,
      secure: process.env.NODE_ENV === 'production',
      sameSite: 'strict',
      maxAge: 24 * 60 * 60 * 1000 // 24 hours
    });

    res.json({
      success: true,
      user: {
        username: user.username,
        role: user.role
      }
    });
  } catch (error) {
    console.error('Login error:', error);
    res.status(500).json({ error: 'Internal server error' });
  }
});

// Logout endpoint
app.post('/api/logout', (req, res) => {
  res.clearCookie('token');
  res.json({ success: true });
});

// Get current user info
app.get('/api/me', requireAuth, (req, res) => {
  res.json({
    username: req.user.username,
    role: req.user.role
  });
});

// Middleware to check if user is authenticated, redirect to login if not
// Supports both internal (proxy-generated) and external (SSO/IDP) tokens
app.use(async (req, res, next) => {
  // Skip auth check for login page and API endpoints
  if (req.path === '/login' || req.path.startsWith('/api/login')) {
    return next();
  }

  const token = req.cookies.token || req.headers.authorization?.replace('Bearer ', '');

  if (!token) {
    // If it's an API call, return 401
    if (req.path.startsWith('/api/') || req.xhr || req.headers.accept?.includes('application/json')) {
      return res.status(401).json({ error: 'Authentication required' });
    }
    // Otherwise redirect to login page
    return res.redirect('/login');
  }

  // Try external token verification first (SSO passthrough)
  if (isExternalAuthEnabled()) {
    const externalUser = await verifyExternalToken(token);
    if (externalUser) {
      req.user = externalUser;
      req.originalToken = token;  // Keep original token for passthrough
      return next();
    }
  }

  // Fall back to internal (proxy-generated) token verification
  const user = verifyToken(token);

  if (!user) {
    if (req.path.startsWith('/api/') || req.xhr || req.headers.accept?.includes('application/json')) {
      return res.status(401).json({ error: 'Invalid or expired token' });
    }
    return res.redirect('/login');
  }

  req.user = user;
  next();
});

// Proxy all other requests to Queen server with RBAC
app.use('/', 
  requireAuth,
  checkMethodAccess,
  createProxyMiddleware({
    target: QUEEN_SERVER_URL,
    changeOrigin: true,
    ws: true,
    logLevel: 'silent',
    onProxyReq: (proxyReq, req, res) => {
      // Remove large/unnecessary headers that can cause "Request Header Fields Too Large" errors
      proxyReq.removeHeader('cookie');
      proxyReq.removeHeader('referer');
      
      // Forward JWT token to Queen server for authentication
      // For external tokens (SSO), pass through the original token unchanged
      // For internal tokens, get from cookie
      let token;
      if (req.originalToken) {
        // External SSO token - pass through as-is
        token = req.originalToken;
      } else {
        // Internal proxy token
        token = req.cookies.token || req.headers.authorization?.replace('Bearer ', '');
      }
      
      if (token) {
        proxyReq.setHeader('Authorization', `Bearer ${token}`);
      }
      
      // Also add user info headers for backward compatibility / logging
      proxyReq.setHeader('X-Proxy-User', req.user.username || req.user.subject || 'unknown');
      proxyReq.setHeader('X-Proxy-Role', req.user.role || 'read-only');
      if (req.user.isExternal) {
        proxyReq.setHeader('X-Proxy-External', 'true');
      }
      
      // Re-stream body if it was consumed by express.json()
      if (req.body && Object.keys(req.body).length > 0) {
        const bodyData = JSON.stringify(req.body);
        proxyReq.setHeader('Content-Type', 'application/json');
        proxyReq.setHeader('Content-Length', Buffer.byteLength(bodyData));
        proxyReq.write(bodyData);
      }
    },
    onError: (err, req, res) => {
      console.error('Proxy error:', err);
      res.status(502).json({ error: 'Bad gateway - Queen server unreachable' });
    }
  })
);

async function startServer() {
  try {
    await initDatabase();
    
    app.listen(PORT, () => {
      console.log(`Queen Proxy listening on port ${PORT}`);
      console.log(`  Target: ${QUEEN_SERVER_URL}`);
      if (isExternalAuthEnabled()) {
        console.log(`  External SSO: enabled (JWKS passthrough)`);
        console.log(`    JWKS URL: ${process.env.EXTERNAL_JWKS_URL || process.env.JWT_JWKS_URL}`);
      } else {
        console.log(`  External SSO: disabled (internal auth only)`);
      }
    });
  } catch (error) {
    console.error('Failed to start server:', error);
    process.exit(1);
  }
}

startServer();
