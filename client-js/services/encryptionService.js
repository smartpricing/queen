import crypto from 'crypto';
import { log } from '../utils/logger.js';

const ALGORITHM = 'aes-256-gcm';
const KEY_ENV = 'QUEEN_ENCRYPTION_KEY';

// Get and validate the encryption key
const getKey = () => {
  const keyHex = process.env[KEY_ENV];
  if (!keyHex) return null;
  
  try {
    const key = Buffer.from(keyHex, 'hex');
    if (key.length !== 32) {
      log(`ERROR: ${KEY_ENV} must be 32 bytes (64 hex characters)`);
      return null;
    }
    return key;
  } catch (error) {
    log(`ERROR: Invalid ${KEY_ENV} format:`, error.message);
    return null;
  }
};

// Encrypt a payload
export const encryptPayload = async (payload) => {
  const key = getKey();
  if (!key) throw new Error('Encryption key not configured');
  
  const iv = crypto.randomBytes(16);
  const cipher = crypto.createCipheriv(ALGORITHM, key, iv);
  
  const payloadStr = JSON.stringify(payload);
  const encrypted = Buffer.concat([
    cipher.update(payloadStr, 'utf8'),
    cipher.final()
  ]);
  
  return {
    encrypted: encrypted.toString('base64'),
    iv: iv.toString('base64'),
    authTag: cipher.getAuthTag().toString('base64')
  };
};

// Decrypt a payload
export const decryptPayload = async (encryptedData) => {
  const key = getKey();
  if (!key) throw new Error('Encryption key not configured');
  
  const decipher = crypto.createDecipheriv(
    ALGORITHM,
    key,
    Buffer.from(encryptedData.iv, 'base64')
  );
  
  decipher.setAuthTag(Buffer.from(encryptedData.authTag, 'base64'));
  
  const decrypted = Buffer.concat([
    decipher.update(Buffer.from(encryptedData.encrypted, 'base64')),
    decipher.final()
  ]);
  
  return JSON.parse(decrypted.toString('utf8'));
};

// Check if encryption is available
export const isEncryptionEnabled = () => {
  const key = getKey();
  return key !== null;
};

// Initialize and log status
export const initEncryption = () => {
  const enabled = isEncryptionEnabled();
  if (enabled) {
    log('✅ Encryption service initialized');
  } else {
    log('⚠️  Encryption service disabled (QUEEN_ENCRYPTION_KEY not set)');
  }
  return enabled;
};
