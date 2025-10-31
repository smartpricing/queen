/**
 * OperationBuilder - Builds operation objects for execution plan
 */

export class OperationBuilder {
  /**
   * Build a filter operation
   * @param {Object} predicate - Predicate specification
   * @returns {Object}
   */
  static filter(predicate) {
    return {
      type: 'filter',
      predicate
    }
  }

  /**
   * Build a map operation
   * @param {Object} fields - Field mappings
   * @returns {Object}
   */
  static map(fields) {
    return {
      type: 'map',
      fields
    }
  }

  /**
   * Build a groupBy operation
   * @param {Array<string>} keys - Group by keys
   * @returns {Object}
   */
  static groupBy(keys) {
    return {
      type: 'groupBy',
      keys
    }
  }

  /**
   * Build an aggregate operation
   * @param {Object} aggregations - Aggregation specifications
   * @returns {Object}
   */
  static aggregate(aggregations) {
    return {
      type: 'aggregate',
      aggregations
    }
  }

  /**
   * Build a distinct operation
   * @param {string} field - Field to deduplicate by
   * @returns {Object}
   */
  static distinct(field) {
    return {
      type: 'distinct',
      field
    }
  }

  /**
   * Build a limit operation
   * @param {number} n - Limit count
   * @returns {Object}
   */
  static limit(n) {
    return {
      type: 'limit',
      limit: n
    }
  }

  /**
   * Build a skip operation
   * @param {number} n - Skip count
   * @returns {Object}
   */
  static skip(n) {
    return {
      type: 'skip',
      skip: n
    }
  }
}

