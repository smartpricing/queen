/**
 * GroupedStream - Stream after groupBy operation
 * Provides aggregation methods
 * 
 * Note: We store the parent stream instance and delegate to it
 * rather than using inheritance with private fields
 */

import { Stream } from './Stream.js'
import { OperationBuilder } from './OperationBuilder.js'
import * as logger from '../utils/logger.js'

export class GroupedStream {
  #parentStream

  constructor(queen, httpClient, source, operations, options) {
    // Store parent stream
    this.#parentStream = new Stream(queen, httpClient, source, operations, options)
  }

  // ========== AGGREGATIONS ==========

  /**
   * Count messages per group
   * @returns {Stream}
   */
  count() {
    logger.log('GroupedStream.count')
    return this.aggregate({
      count: { $count: '*' }
    })
  }

  /**
   * Sum field values per group
   * @param {string} field - Field to sum
   * @returns {Stream}
   */
  sum(field) {
    logger.log('GroupedStream.sum', { field })
    return this.aggregate({
      sum: { $sum: field }
    })
  }

  /**
   * Average field values per group
   * @param {string} field - Field to average
   * @returns {Stream}
   */
  avg(field) {
    logger.log('GroupedStream.avg', { field })
    return this.aggregate({
      avg: { $avg: field }
    })
  }

  /**
   * Minimum field value per group
   * @param {string} field - Field to get minimum
   * @returns {Stream}
   */
  min(field) {
    logger.log('GroupedStream.min', { field })
    return this.aggregate({
      min: { $min: field }
    })
  }

  /**
   * Maximum field value per group
   * @param {string} field - Field to get maximum
   * @returns {Stream}
   */
  max(field) {
    logger.log('GroupedStream.max', { field })
    return this.aggregate({
      max: { $max: field }
    })
  }

  /**
   * Perform multiple aggregations
   * @param {Object} aggregations - Aggregation specifications
   * @returns {Stream}
   */
  aggregate(aggregations) {
    logger.log('GroupedStream.aggregate', { aggregationCount: Object.keys(aggregations).length })
    
    const operation = OperationBuilder.aggregate(aggregations)
    
    // Add aggregation operation to parent stream
    return this.#parentStream._addOperation(operation)
  }

  // Delegate common Stream methods
  async execute() {
    return this.#parentStream.execute()
  }

  async *[Symbol.asyncIterator]() {
    yield* this.#parentStream[Symbol.asyncIterator]()
  }

  async take(n) {
    return this.#parentStream.take(n)
  }

  async collect() {
    return this.#parentStream.collect()
  }

  async outputTo(destination) {
    return this.#parentStream.outputTo(destination)
  }
}

