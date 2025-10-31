import { verifyToken } from './auth.js';

// Authentication middleware - checks if user has valid JWT
export function requireAuth(req, res, next) {
  const token = req.cookies.token || req.headers.authorization?.replace('Bearer ', '');

  if (!token) {
    return res.status(401).json({ error: 'Authentication required' });
  }

  const user = verifyToken(token);

  if (!user) {
    return res.status(401).json({ error: 'Invalid or expired token' });
  }

  req.user = user;
  next();
}

// RBAC middleware - checks if user has required role
export function requireRole(allowedRoles) {
  return (req, res, next) => {
    if (!req.user) {
      return res.status(401).json({ error: 'Authentication required' });
    }

    if (!allowedRoles.includes(req.user.role)) {
      return res.status(403).json({ error: 'Insufficient permissions' });
    }

    next();
  };
}

// Method-based access control
export function checkMethodAccess(req, res, next) {
  const method = req.method;
  const role = req.user?.role;

  if (!role) {
    return res.status(401).json({ error: 'Authentication required' });
  }

  // Admin can do everything
  if (role === 'admin') {
    return next();
  }

  // Read-write can do GET, POST, PUT, DELETE
  if (role === 'read-write') {
    return next();
  }

  // Read-only can only do GET
  if (role === 'read-only' && method === 'GET') {
    return next();
  }

  return res.status(403).json({ 
    error: 'Insufficient permissions',
    detail: `Role '${role}' cannot perform ${method} operations`
  });
}

