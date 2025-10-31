/**
 * PredicateBuilder - Builds predicate objects from filter conditions
 */

export class PredicateBuilder {
  /**
   * Build a predicate from object syntax
   * @param {Object} obj - Filter condition object
   * @returns {Object}
   */
  static build(obj) {
    if (Array.isArray(obj)) {
      // Array of conditions = AND
      return {
        type: 'logical',
        operator: 'AND',
        children: obj.map(p => PredicateBuilder.build(p))
      }
    }

    const predicates = []

    for (const [field, condition] of Object.entries(obj)) {
      if (typeof condition === 'object' && condition !== null && !Array.isArray(condition)) {
        // Operator object: { $gt: 1000 }
        for (const [op, value] of Object.entries(condition)) {
          predicates.push({
            type: 'comparison',
            field,
            operator: PredicateBuilder.#mapOperator(op),
            value
          })
        }
      } else {
        // Simple equality: { status: 'completed' }
        predicates.push({
          type: 'comparison',
          field,
          operator: '=',
          value: condition
        })
      }
    }

    if (predicates.length === 0) {
      throw new Error('Empty predicate object')
    }

    if (predicates.length === 1) {
      return predicates[0]
    }

    return {
      type: 'logical',
      operator: 'AND',
      children: predicates
    }
  }

  /**
   * Map operator shortcuts to SQL operators
   * @private
   */
  static #mapOperator(op) {
    const mapping = {
      '$eq': '=',
      '$ne': '!=',
      '$gt': '>',
      '$gte': '>=',
      '$lt': '<',
      '$lte': '<=',
      '$in': 'IN',
      '$nin': 'NOT IN',
      '$contains': '@>',    // JSONB contains
      '$contained': '<@',   // JSONB contained by
      '$exists': '?',       // JSONB key exists
      '$regex': '~',        // Regex match
      '$iregex': '~*'       // Case-insensitive regex
    }

    return mapping[op] || op
  }
}

