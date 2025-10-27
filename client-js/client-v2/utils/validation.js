/**
 * Validation utilities
 */

const UUID_V4_REGEX = /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/i

export function isValidUUID(str) {
  return typeof str === 'string' && UUID_V4_REGEX.test(str)
}

export function validateQueueName(name) {
  if (typeof name !== 'string' || name.trim().length === 0) {
    throw new Error('Queue name must be a non-empty string')
  }
  return name.trim()
}

export function validateUrl(url) {
  if (typeof url !== 'string' || !url.startsWith('http')) {
    throw new Error(`Invalid URL: ${url}`)
  }
  return url
}

export function validateUrls(urls) {
  if (!Array.isArray(urls) || urls.length === 0) {
    throw new Error('URLs must be a non-empty array')
  }
  return urls.map(validateUrl)
}

