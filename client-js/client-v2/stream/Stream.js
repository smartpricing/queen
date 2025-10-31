/**
 * Stream - Base stream processing class
 * Provides fluent API for building streaming pipelines
 */

import { OperationBuilder } from './OperationBuilder.js'
import { PredicateBuilder } from './PredicateBuilder.js'
import { Serializer } from './Serializer.js'
import { GroupedStream } from './GroupedStream.js'
import * as logger from '../utils/logger.js'

export class Stream {
  #queen
  #httpClient
  #source
  #operations
  #options

  constructor(queen, httpClient, source, operations = [], options = {}) {
    this.#queen = queen
    this.#httpClient = httpClient
    this.#source = source
    this.#operations = operations
    this.#options = options
  }

  // ========== FILTERING ==========

  /**
   * Filter messages based on a predicate
   * @param {Function|Object} predicate - Filter function or predicate object
   * @returns {Stream}
   */
  filter(predicate) {
    logger.log('Stream.filter', { predicateType: typeof predicate })
    
    const operation = typeof predicate === 'function'
      ? OperationBuilder.filter(Serializer.serializePredicate(predicate))
      : OperationBuilder.filter(PredicateBuilder.build(predicate))

    return new Stream(
      this.#queen,
      this.#httpClient,
      this.#source,
      [...this.#operations, operation],
      this.#options
    )
  }

  // ========== TRANSFORMATION ==========

  /**
   * Transform messages by mapping fields
   * @param {Function|Object} mapper - Mapper function or field mapping object
   * @returns {Stream}
   */
  map(mapper) {
    logger.log('Stream.map', { mapperType: typeof mapper })
    
    const operation = typeof mapper === 'function'
      ? OperationBuilder.map(Serializer.serializeMapper(mapper))
      : OperationBuilder.map(mapper)

    return new Stream(
      this.#queen,
      this.#httpClient,
      this.#source,
      [...this.#operations, operation],
      this.#options
    )
  }

  /**
   * Map only the payload, preserving message metadata
   * @param {Function|Object} mapper - Mapper for payload
   * @returns {Stream}
   */
  mapValues(mapper) {
    logger.log('Stream.mapValues', { mapperType: typeof mapper })
    
    return this.map(msg => ({
      ...msg,
      payload: typeof mapper === 'function' ? mapper(msg.payload) : mapper
    }))
  }

  // ========== GROUPING ==========

  /**
   * Group messages by key(s)
   * @param {string|string[]} key - Field(s) to group by
   * @returns {GroupedStream}
   */
  groupBy(key) {
    logger.log('Stream.groupBy', { key })
    
    const operation = OperationBuilder.groupBy(
      Array.isArray(key) ? key : [key]
    )

    return new GroupedStream(
      this.#queen,
      this.#httpClient,
      this.#source,
      [...this.#operations, operation],
      this.#options
    )
  }

  // ========== UTILITY OPERATIONS ==========

  /**
   * Deduplicate messages by field
   * @param {string} field - Field to use for deduplication
   * @returns {Stream}
   */
  distinct(field) {
    logger.log('Stream.distinct', { field })
    
    const operation = OperationBuilder.distinct(field)
    return new Stream(
      this.#queen,
      this.#httpClient,
      this.#source,
      [...this.#operations, operation],
      this.#options
    )
  }

  /**
   * Limit number of messages
   * @param {number} n - Maximum number of messages
   * @returns {Stream}
   */
  limit(n) {
    logger.log('Stream.limit', { n })
    
    const operation = OperationBuilder.limit(n)
    return new Stream(
      this.#queen,
      this.#httpClient,
      this.#source,
      [...this.#operations, operation],
      this.#options
    )
  }

  /**
   * Skip first n messages
   * @param {number} n - Number of messages to skip
   * @returns {Stream}
   */
  skip(n) {
    logger.log('Stream.skip', { n })
    
    const operation = OperationBuilder.skip(n)
    return new Stream(
      this.#queen,
      this.#httpClient,
      this.#source,
      [...this.#operations, operation],
      this.#options
    )
  }

  // ========== OUTPUT ==========

  /**
   * Output stream results to a queue
   * @param {string} destination - Destination queue name
   * @returns {Promise<string>} Stream ID
   */
  async outputTo(destination) {
    logger.log('Stream.outputTo', { destination })
    
    const plan = this.#buildExecutionPlan({ destination })
    const response = await this.#httpClient.post('/api/v1/stream/start', plan)
    
    logger.log('Stream.outputTo', { streamId: response.streamId, status: 'started' })
    return response.streamId
  }

  // ========== CONSUMPTION ==========

  /**
   * Execute stream query and return results
   * @returns {Promise<Object>} Query results
   */
  async execute() {
    logger.log('Stream.execute', { source: this.#source, operationCount: this.#operations.length })
    
    const plan = this.#buildExecutionPlan()
    const result = await this.#httpClient.post('/api/v1/stream/query', plan)
    
    logger.log('Stream.execute', { messageCount: result.messages?.length || 0 })
    return result
  }

  /**
   * Async iterator for streaming results
   * @yields {Object} Stream messages
   */
  async *[Symbol.asyncIterator]() {
    logger.log('Stream.asyncIterator', { source: this.#source })
    
    const plan = this.#buildExecutionPlan()
    
    // Use streaming endpoint
    const response = await this.#httpClient.fetch('/api/v1/stream/consume', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(plan)
    })

    if (!response.ok) {
      const error = await response.text()
      throw new Error(`Stream consume failed: ${error}`)
    }

    // Read response as NDJSON
    const reader = response.body.getReader()
    const decoder = new TextDecoder()
    let buffer = ''

    try {
      while (true) {
        const { done, value } = await reader.read()
        
        if (done) break

        buffer += decoder.decode(value, { stream: true })
        const lines = buffer.split('\n')
        buffer = lines.pop() // Keep incomplete line in buffer

        for (const line of lines) {
          if (!line.trim()) continue
          
          try {
            const message = JSON.parse(line)
            yield message
          } catch (err) {
            logger.error('Stream.asyncIterator', { error: 'Failed to parse line', line })
          }
        }
      }

      // Process remaining buffer
      if (buffer.trim()) {
        const message = JSON.parse(buffer)
        yield message
      }
    } finally {
      reader.releaseLock()
    }
  }

  /**
   * Collect first n messages into array
   * @param {number} n - Number of messages to collect
   * @returns {Promise<Array>}
   */
  async take(n) {
    logger.log('Stream.take', { n })
    
    const results = []
    let count = 0

    for await (const message of this.limit(n)) {
      results.push(message)
      if (++count >= n) break
    }

    logger.log('Stream.take', { collected: results.length })
    return results
  }

  /**
   * Collect all messages into array
   * @returns {Promise<Array>}
   */
  async collect() {
    logger.log('Stream.collect', 'Starting collection')
    
    const results = []
    
    for await (const message of this) {
      results.push(message)
    }

    logger.log('Stream.collect', { collected: results.length })
    return results
  }

  // ========== EXECUTION PLAN ==========

  #buildExecutionPlan(output = {}) {
    const [queue, consumerGroup, partition] = this.#parseSource(this.#source)

    const plan = {
      source: queue,
      consumerGroup: consumerGroup || '__STREAM__',
      partition: partition || null,
      operations: this.#operations,
      destination: output.destination || null,
      outputTable: output.outputTable || null,
      outputMode: output.outputMode || 'append',
      batchSize: this.#options.batchSize || 100,
      autoAck: this.#options.autoAck !== false,
      from: this.#options.from || null,  // Time filter: 'latest', ISO timestamp, or Date
      to: this.#options.to || null        // End time filter (optional)
    }

    logger.log('Stream.buildExecutionPlan', { 
      queue, 
      consumerGroup: plan.consumerGroup, 
      operationCount: this.#operations.length,
      from: plan.from,
      to: plan.to
    })

    return plan
  }

  #parseSource(source) {
    // Parse "queue@group/partition" format
    const [queuePart, partition] = source.split('/')
    const [queue, group] = queuePart.split('@')

    return [queue, group, partition]
  }

  // Internal method for adding operations (used by GroupedStream)
  _addOperation(operation) {
    return new Stream(
      this.#queen,
      this.#httpClient,
      this.#source,
      [...this.#operations, operation],
      this.#options
    )
  }
}

