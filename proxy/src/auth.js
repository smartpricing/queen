import bcrypt from 'bcrypt';
import jwt from 'jsonwebtoken';
import * as jose from 'jose';
import { getUserByUsername } from './db.js';

const JWT_SECRET = process.env.JWT_SECRET || 'change-me-in-production';
const JWT_EXPIRES_IN = process.env.JWT_EXPIRES_IN || '24h';

// External IDP configuration for SSO passthrough
const EXTERNAL_JWKS_URL = process.env.EXTERNAL_JWKS_URL || process.env.JWT_JWKS_URL || '';
const EXTERNAL_ISSUER = process.env.EXTERNAL_ISSUER || process.env.JWT_ISSUER || '';
const EXTERNAL_AUDIENCE = process.env.EXTERNAL_AUDIENCE || process.env.JWT_AUDIENCE || '';

// Cache for JWKS
let jwksCache = null;
let jwksCacheTime = 0;
const JWKS_CACHE_TTL = 3600 * 1000; // 1 hour

/**
 * Get or refresh JWKS from external IDP
 */
async function getJWKS() {
  if (!EXTERNAL_JWKS_URL) {
    return null;
  }

  const now = Date.now();
  if (jwksCache && (now - jwksCacheTime) < JWKS_CACHE_TTL) {
    return jwksCache;
  }

  try {
    jwksCache = jose.createRemoteJWKSet(new URL(EXTERNAL_JWKS_URL));
    jwksCacheTime = now;
    console.log('[Auth] JWKS loaded from:', EXTERNAL_JWKS_URL);
    return jwksCache;
  } catch (error) {
    console.error('[Auth] Failed to load JWKS:', error.message);
    return null;
  }
}

/**
 * Verify token from external IDP (SSO passthrough)
 * Supports RS256, EdDSA (Ed25519), and other algorithms via JWKS
 * 
 * @param {string} token - JWT token to verify
 * @returns {object|null} - Decoded claims or null if invalid
 */
export async function verifyExternalToken(token) {
  if (!EXTERNAL_JWKS_URL) {
    return null;
  }

  try {
    const jwks = await getJWKS();
    if (!jwks) {
      return null;
    }

    const options = {};
    
    // Add issuer validation if configured
    if (EXTERNAL_ISSUER) {
      options.issuer = EXTERNAL_ISSUER;
    }
    
    // Add audience validation if configured
    if (EXTERNAL_AUDIENCE) {
      options.audience = EXTERNAL_AUDIENCE;
    }

    const { payload, protectedHeader } = await jose.jwtVerify(token, jwks, options);
    
    console.log('[Auth] External token verified, alg:', protectedHeader.alg, 'sub:', payload.sub);
    
    // Normalize claims to match internal token format
    return {
      id: payload.sub || payload.id,
      username: payload.preferred_username || payload.username || payload.name || payload.sub,
      role: payload.role || (payload.roles && payload.roles[0]) || 'read-only',
      roles: payload.roles || (payload.role ? [payload.role] : ['read-only']),
      subject: payload.sub,
      issuer: payload.iss,
      isExternal: true  // Flag to indicate this is an external token
    };
  } catch (error) {
    // Don't log expected failures (invalid/expired tokens)
    if (error.code !== 'ERR_JWT_EXPIRED' && error.code !== 'ERR_JWS_SIGNATURE_VERIFICATION_FAILED') {
      console.debug('[Auth] External token verification failed:', error.message);
    }
    return null;
  }
}

export async function authenticateUser(username, password) {
  const user = await getUserByUsername(username);
  
  if (!user) {
    return null;
  }

  const isValid = await bcrypt.compare(password, user.password_hash);
  
  if (!isValid) {
    return null;
  }

  return {
    id: user.id,
    username: user.username,
    role: user.role
  };
}

export function generateToken(user, expiresIn = JWT_EXPIRES_IN) {
  const options = {};
  if (expiresIn && expiresIn !== 'never') {
    options.expiresIn = expiresIn;
  }
  return jwt.sign(
    {
      id: user.id,
      username: user.username,
      role: user.role
    },
    JWT_SECRET,
    options
  );
}

export function verifyToken(token) {
  try {
    return jwt.verify(token, JWT_SECRET);
  } catch (error) {
    return null;
  }
}

export async function hashPassword(password) {
  return bcrypt.hash(password, 10);
}

/**
 * Check if external SSO is configured
 */
export function isExternalAuthEnabled() {
  return !!EXTERNAL_JWKS_URL;
}

