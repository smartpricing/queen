/**
 * Logger Unit Tests
 *
 * Tests the pluggable logger system in utils/logger.js:
 * - Default console-based logging (gated by QUEEN_CLIENT_LOG)
 * - Custom logger injection via configure()
 * - Level mapping (log->info, warn->warn, error->error, debug->debug)
 * - debug() fallback to info() when custom logger has no debug()
 * - configure(null) resets to default
 * - configure() validation
 * - isEnabled() behavior
 *
 * These are pure unit tests — no Queen server required.
 */

import * as logger from '../client-v2/utils/logger.js'

function createMockLogger() {
  const calls = { info: [], warn: [], error: [], debug: [] }
  return {
    calls,
    info: (msg) => calls.info.push(msg),
    warn: (msg) => calls.warn.push(msg),
    error: (msg) => calls.error.push(msg),
    debug: (msg) => calls.debug.push(msg)
  }
}

function createMockLoggerWithoutDebug() {
  const calls = { info: [], warn: [], error: [] }
  return {
    calls,
    info: (msg) => calls.info.push(msg),
    warn: (msg) => calls.warn.push(msg),
    error: (msg) => calls.error.push(msg)
  }
}

/**
 * Test that configure() accepts a valid logger and routes log() to info()
 */
export async function testLoggerCustomLogRouting(_client) {
  const mock = createMockLogger()
  logger.configure(mock)

  try {
    logger.log('TestOp', 'hello')
    logger.log('TestOp', { key: 'value' })

    if (mock.calls.info.length !== 2) {
      return { success: false, message: `Expected 2 info calls, got ${mock.calls.info.length}` }
    }
    if (!mock.calls.info[0].includes('[TestOp]') || !mock.calls.info[0].includes('hello')) {
      return { success: false, message: `Unexpected info message: ${mock.calls.info[0]}` }
    }
    if (!mock.calls.info[1].includes('"key":"value"')) {
      return { success: false, message: `Object details not serialized: ${mock.calls.info[1]}` }
    }

    return { success: true, message: 'log() routes to custom logger info()' }
  } finally {
    logger.configure(null)
  }
}

/**
 * Test that warn() routes to custom logger warn()
 */
export async function testLoggerCustomWarnRouting(_client) {
  const mock = createMockLogger()
  logger.configure(mock)

  try {
    logger.warn('WarnOp', 'something bad')
    logger.warn('WarnOp', { code: 42 })

    if (mock.calls.warn.length !== 2) {
      return { success: false, message: `Expected 2 warn calls, got ${mock.calls.warn.length}` }
    }
    if (!mock.calls.warn[0].includes('[WarnOp]')) {
      return { success: false, message: `Missing operation in warn: ${mock.calls.warn[0]}` }
    }

    return { success: true, message: 'warn() routes to custom logger warn()' }
  } finally {
    logger.configure(null)
  }
}

/**
 * Test that error() routes to custom logger error()
 */
export async function testLoggerCustomErrorRouting(_client) {
  const mock = createMockLogger()
  logger.configure(mock)

  try {
    logger.error('ErrOp', { error: 'boom' })

    if (mock.calls.error.length !== 1) {
      return { success: false, message: `Expected 1 error call, got ${mock.calls.error.length}` }
    }
    if (!mock.calls.error[0].includes('[ErrOp]') || !mock.calls.error[0].includes('boom')) {
      return { success: false, message: `Unexpected error message: ${mock.calls.error[0]}` }
    }

    return { success: true, message: 'error() routes to custom logger error()' }
  } finally {
    logger.configure(null)
  }
}

/**
 * Test that debug() routes to custom logger debug() when available
 */
export async function testLoggerCustomDebugRouting(_client) {
  const mock = createMockLogger()
  logger.configure(mock)

  try {
    logger.debug('DebugOp', 'verbose detail')

    if (mock.calls.debug.length !== 1) {
      return { success: false, message: `Expected 1 debug call, got ${mock.calls.debug.length}` }
    }
    if (!mock.calls.debug[0].includes('[DebugOp]')) {
      return { success: false, message: `Missing operation in debug: ${mock.calls.debug[0]}` }
    }
    // info() should NOT have been called
    if (mock.calls.info.length !== 0) {
      return { success: false, message: `debug() should not fall back to info() when debug() exists` }
    }

    return { success: true, message: 'debug() routes to custom logger debug()' }
  } finally {
    logger.configure(null)
  }
}

/**
 * Test that debug() falls back to info() when custom logger has no debug()
 */
export async function testLoggerDebugFallbackToInfo(_client) {
  const mock = createMockLoggerWithoutDebug()
  logger.configure(mock)

  try {
    logger.debug('FallbackOp', 'should go to info')

    if (mock.calls.info.length !== 1) {
      return { success: false, message: `Expected 1 info call (fallback), got ${mock.calls.info.length}` }
    }
    if (!mock.calls.info[0].includes('[FallbackOp]')) {
      return { success: false, message: `Missing operation in fallback: ${mock.calls.info[0]}` }
    }

    return { success: true, message: 'debug() falls back to info() when no debug() on logger' }
  } finally {
    logger.configure(null)
  }
}

/**
 * Test that configure(null) resets to default console behavior
 */
export async function testLoggerConfigureNull(_client) {
  const mock = createMockLogger()
  logger.configure(mock)

  // Verify custom logger is active
  logger.log('Before', 'test')
  if (mock.calls.info.length !== 1) {
    return { success: false, message: 'Custom logger not active before reset' }
  }

  // Reset
  logger.configure(null)

  // Now log should NOT go to mock anymore
  logger.log('After', 'test')
  if (mock.calls.info.length !== 1) {
    return { success: false, message: `Expected still 1 info call after reset, got ${mock.calls.info.length}` }
  }

  return { success: true, message: 'configure(null) resets to default' }
}

/**
 * Test that configure() throws on invalid logger (missing info method)
 */
export async function testLoggerConfigureValidation(_client) {
  try {
    logger.configure({ warn: () => {}, error: () => {} })
    return { success: false, message: 'Should have thrown for logger missing info()' }
  } catch (e) {
    if (!e.message.includes('info()')) {
      return { success: false, message: `Wrong error message: ${e.message}` }
    }
  } finally {
    logger.configure(null)
  }

  // Empty object should also fail
  try {
    logger.configure({})
    return { success: false, message: 'Should have thrown for empty object' }
  } catch (e) {
    // expected
  } finally {
    logger.configure(null)
  }

  return { success: true, message: 'configure() validates logger interface' }
}

/**
 * Test isEnabled() returns true when custom logger is set
 */
export async function testLoggerIsEnabledWithCustom(_client) {
  // Without custom logger and without QUEEN_CLIENT_LOG, isEnabled depends on env
  const beforeCustom = logger.isEnabled()

  const mock = createMockLogger()
  logger.configure(mock)

  try {
    if (!logger.isEnabled()) {
      return { success: false, message: 'isEnabled() should return true with custom logger' }
    }
    return { success: true, message: `isEnabled() returns true with custom logger (was ${beforeCustom} before)` }
  } finally {
    logger.configure(null)
  }
}

/**
 * Test that levels are isolated — log() only hits info(), not warn() or error()
 */
export async function testLoggerLevelIsolation(_client) {
  const mock = createMockLogger()
  logger.configure(mock)

  try {
    logger.log('Op', 'info msg')
    logger.warn('Op', 'warn msg')
    logger.error('Op', 'error msg')
    logger.debug('Op', 'debug msg')

    if (mock.calls.info.length !== 1) {
      return { success: false, message: `info: expected 1, got ${mock.calls.info.length}` }
    }
    if (mock.calls.warn.length !== 1) {
      return { success: false, message: `warn: expected 1, got ${mock.calls.warn.length}` }
    }
    if (mock.calls.error.length !== 1) {
      return { success: false, message: `error: expected 1, got ${mock.calls.error.length}` }
    }
    if (mock.calls.debug.length !== 1) {
      return { success: false, message: `debug: expected 1, got ${mock.calls.debug.length}` }
    }

    return { success: true, message: 'Each level routes to exactly its corresponding method' }
  } finally {
    logger.configure(null)
  }
}

/**
 * Test that string details are passed through without JSON.stringify
 */
export async function testLoggerStringDetails(_client) {
  const mock = createMockLogger()
  logger.configure(mock)

  try {
    logger.log('Op', 'plain string')

    const msg = mock.calls.info[0]
    if (!msg.includes('plain string')) {
      return { success: false, message: `String details not passed through: ${msg}` }
    }
    // Should NOT be double-quoted as JSON
    if (msg.includes('"plain string"')) {
      return { success: false, message: `String was JSON-serialized: ${msg}` }
    }

    return { success: true, message: 'String details passed as-is' }
  } finally {
    logger.configure(null)
  }
}

/**
 * Test that object details are JSON-serialized
 */
export async function testLoggerObjectDetails(_client) {
  const mock = createMockLogger()
  logger.configure(mock)

  try {
    logger.log('Op', { foo: 'bar', count: 3 })

    const msg = mock.calls.info[0]
    if (!msg.includes('"foo":"bar"') || !msg.includes('"count":3')) {
      return { success: false, message: `Object not serialized correctly: ${msg}` }
    }

    return { success: true, message: 'Object details JSON-serialized' }
  } finally {
    logger.configure(null)
  }
}
