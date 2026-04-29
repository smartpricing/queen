import crypto from 'crypto';
import * as jose from 'jose';
import {
  createGoogleUser,
  getUserByEmail,
  getUserByGoogleSub,
  linkGoogleAccount,
} from './db.js';

const GOOGLE_CLIENT_ID = process.env.GOOGLE_CLIENT_ID || '';
const GOOGLE_CLIENT_SECRET = process.env.GOOGLE_CLIENT_SECRET || '';
const GOOGLE_REDIRECT_URI = process.env.GOOGLE_REDIRECT_URI || '';

// Comma-separated list of allowed Google Workspace domains (matched against the
// `hd` claim). Empty = allow any verified email.
const GOOGLE_ALLOWED_DOMAINS = (process.env.GOOGLE_ALLOWED_DOMAINS || '')
  .split(',')
  .map((d) => d.trim().toLowerCase())
  .filter(Boolean);

const GOOGLE_AUTO_PROVISION = (process.env.GOOGLE_AUTO_PROVISION || 'false').toLowerCase() === 'true';
const GOOGLE_DEFAULT_ROLE = process.env.GOOGLE_DEFAULT_ROLE || 'read-only';

const ALLOWED_ROLES = new Set(['admin', 'read-write', 'read-only']);

const GOOGLE_AUTHORIZE_URL = 'https://accounts.google.com/o/oauth2/v2/auth';
const GOOGLE_TOKEN_URL = 'https://oauth2.googleapis.com/token';
const GOOGLE_JWKS_URL = 'https://www.googleapis.com/oauth2/v3/certs';
const GOOGLE_VALID_ISSUERS = ['https://accounts.google.com', 'accounts.google.com'];

let jwksCache = null;
function getJWKS() {
  if (!jwksCache) {
    jwksCache = jose.createRemoteJWKSet(new URL(GOOGLE_JWKS_URL));
  }
  return jwksCache;
}

export function isGoogleAuthEnabled() {
  return !!(GOOGLE_CLIENT_ID && GOOGLE_CLIENT_SECRET && GOOGLE_REDIRECT_URI);
}

export function getGoogleConfig() {
  return {
    enabled: isGoogleAuthEnabled(),
    clientId: GOOGLE_CLIENT_ID,
    redirectUri: GOOGLE_REDIRECT_URI,
    allowedDomains: GOOGLE_ALLOWED_DOMAINS,
    autoProvision: GOOGLE_AUTO_PROVISION,
    defaultRole: ALLOWED_ROLES.has(GOOGLE_DEFAULT_ROLE) ? GOOGLE_DEFAULT_ROLE : 'read-only',
  };
}

function randomToken(bytes = 32) {
  return crypto.randomBytes(bytes).toString('hex');
}

/**
 * Build the URL the browser should be redirected to in order to start the
 * Google OAuth 2.0 Authorization Code flow. Returns the URL plus the `state`
 * and `nonce` values that the caller MUST persist (e.g. in a short-lived
 * httpOnly cookie) so they can be cross-checked in the callback.
 */
export function buildAuthorizeUrl({ redirectAfterLogin } = {}) {
  if (!isGoogleAuthEnabled()) {
    throw new Error('Google auth is not configured');
  }

  const state = randomToken();
  const nonce = randomToken();

  const params = new URLSearchParams({
    client_id: GOOGLE_CLIENT_ID,
    redirect_uri: GOOGLE_REDIRECT_URI,
    response_type: 'code',
    scope: 'openid email profile',
    access_type: 'online',
    include_granted_scopes: 'true',
    prompt: 'select_account',
    state,
    nonce,
  });

  // If a single domain is configured, hint Google's account chooser to it.
  if (GOOGLE_ALLOWED_DOMAINS.length === 1) {
    params.set('hd', GOOGLE_ALLOWED_DOMAINS[0]);
  }

  return {
    url: `${GOOGLE_AUTHORIZE_URL}?${params.toString()}`,
    state,
    nonce,
    redirectAfterLogin: redirectAfterLogin || '/',
  };
}

async function exchangeCodeForTokens(code) {
  const body = new URLSearchParams({
    code,
    client_id: GOOGLE_CLIENT_ID,
    client_secret: GOOGLE_CLIENT_SECRET,
    redirect_uri: GOOGLE_REDIRECT_URI,
    grant_type: 'authorization_code',
  });

  const response = await fetch(GOOGLE_TOKEN_URL, {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body: body.toString(),
  });

  if (!response.ok) {
    const text = await response.text().catch(() => '');
    throw new Error(`Google token exchange failed (${response.status}): ${text}`);
  }

  const tokens = await response.json();
  if (!tokens.id_token) {
    throw new Error('Google token response missing id_token');
  }
  return tokens;
}

async function verifyIdToken(idToken, expectedNonce) {
  const { payload } = await jose.jwtVerify(idToken, getJWKS(), {
    issuer: GOOGLE_VALID_ISSUERS,
    audience: GOOGLE_CLIENT_ID,
  });

  if (expectedNonce && payload.nonce !== expectedNonce) {
    throw new Error('Google id_token nonce mismatch');
  }
  if (!payload.sub) {
    throw new Error('Google id_token missing sub');
  }
  if (!payload.email) {
    throw new Error('Google id_token missing email (request the "email" scope)');
  }
  if (payload.email_verified === false) {
    throw new Error('Google email is not verified');
  }

  if (GOOGLE_ALLOWED_DOMAINS.length > 0) {
    const hd = (payload.hd || '').toLowerCase();
    const emailDomain = String(payload.email).split('@')[1]?.toLowerCase() || '';
    const domainOk = GOOGLE_ALLOWED_DOMAINS.includes(hd) || GOOGLE_ALLOWED_DOMAINS.includes(emailDomain);
    if (!domainOk) {
      throw new Error(`Google account domain not allowed: ${hd || emailDomain || '<unknown>'}`);
    }
  }

  return payload;
}

/**
 * Resolve a verified Google identity to a local user record. Linking by email
 * is gated on `email_verified=true` to prevent account takeover by an attacker
 * who registers a Google account with someone else's address.
 */
async function resolveUser(claims) {
  const sub = claims.sub;
  const email = String(claims.email).toLowerCase();

  let user = await getUserByGoogleSub(sub);
  if (user) return user;

  if (claims.email_verified) {
    const existing = await getUserByEmail(email);
    if (existing) {
      return await linkGoogleAccount(existing.id, sub, email);
    }
  }

  if (!GOOGLE_AUTO_PROVISION) {
    const err = new Error('User is not provisioned for this proxy');
    err.code = 'NOT_PROVISIONED';
    throw err;
  }

  const role = ALLOWED_ROLES.has(GOOGLE_DEFAULT_ROLE) ? GOOGLE_DEFAULT_ROLE : 'read-only';
  return await createGoogleUser({ email, googleSub: sub, role, username: email });
}

/**
 * Run the full callback half of the OAuth flow: exchange the code, verify the
 * id_token, then look up / link / provision the local user.
 *
 * @param {object} args
 * @param {string} args.code - The `code` query param Google returned.
 * @param {string} args.expectedNonce - The nonce that was placed in the authorize request.
 * @returns {Promise<{ id, username, role, email }>} - Local user suitable for `generateToken`.
 */
export async function handleGoogleCallback({ code, expectedNonce }) {
  if (!isGoogleAuthEnabled()) {
    throw new Error('Google auth is not configured');
  }
  if (!code) {
    throw new Error('Missing authorization code');
  }

  const tokens = await exchangeCodeForTokens(code);
  const claims = await verifyIdToken(tokens.id_token, expectedNonce);
  const user = await resolveUser(claims);

  return {
    id: user.id,
    username: user.username,
    role: user.role,
    email: user.email,
  };
}
