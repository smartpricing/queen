/**
 * Serializer - Converts JavaScript functions to AST-like structures
 * for server-side execution
 */

export class Serializer {
  /**
   * Serialize a predicate function to AST
   * @param {Function} fn - Predicate function
   * @returns {Object}
   */
  static serializePredicate(fn) {
    const fnStr = fn.toString()

    // Simple parser for arrow functions like: msg => msg.payload.amount > 1000
    const arrowMatch = fnStr.match(/\(?\s*(\w+)\s*\)?\s*=>\s*(.+)/)

    if (arrowMatch) {
      const [, param, body] = arrowMatch

      // Parse the body
      return this.#parseExpression(body.trim(), param)
    }

    throw new Error('Unable to serialize predicate function. Use object syntax instead: { "payload.amount": { $gt: 1000 } }')
  }

  /**
   * Serialize a mapper function to field mapping
   * @param {Function} fn - Mapper function
   * @returns {Object}
   */
  static serializeMapper(fn) {
    const fnStr = fn.toString()
    const arrowMatch = fnStr.match(/\(?\s*(\w+)\s*\)?\s*=>\s*(\{[\s\S]*\}|\(.+\)|.+)/)

    if (arrowMatch) {
      const [, param, body] = arrowMatch

      // For object literals: msg => ({ userId: msg.payload.userId })
      const objectMatch = body.match(/^\(\s*\{([\s\S]*)\}\s*\)$/) || body.match(/^\{([\s\S]*)\}$/)

      if (objectMatch) {
        const fields = this.#parseObjectLiteral(objectMatch[1], param)
        return { type: 'object', fields }
      }

      // For simple expressions: msg => msg.payload.userId
      return { type: 'expression', expr: this.#parseExpression(body, param) }
    }

    throw new Error('Unable to serialize mapper function. Use object syntax instead: { userId: "payload.userId", amount: "payload.amount" }')
  }

  /**
   * Parse an expression
   * @private
   */
  static #parseExpression(expr, param) {
    // Simple expression parser
    // msg.payload.amount > 1000 => { field: 'payload.amount', operator: '>', value: 1000 }

    const comparisonOps = ['>=', '<=', '===', '!==', '==', '!=', '>', '<']

    for (const op of comparisonOps) {
      if (expr.includes(op)) {
        const [left, right] = expr.split(op).map(s => s.trim())

        return {
          type: 'comparison',
          field: this.#extractField(left, param),
          operator: this.#normalizeOperator(op),
          value: this.#parseValue(right)
        }
      }
    }

    // Logical operators
    if (expr.includes('&&')) {
      const parts = expr.split('&&').map(p => this.#parseExpression(p.trim(), param))
      return {
        type: 'logical',
        operator: 'AND',
        children: parts
      }
    }

    if (expr.includes('||')) {
      const parts = expr.split('||').map(p => this.#parseExpression(p.trim(), param))
      return {
        type: 'logical',
        operator: 'OR',
        children: parts
      }
    }

    // Simple field reference
    return {
      type: 'field',
      field: this.#extractField(expr, param)
    }
  }

  /**
   * Extract field path from expression
   * @private
   */
  static #extractField(expr, param) {
    // msg.payload.userId => 'payload.userId'
    if (expr.startsWith(param + '.')) {
      return expr.substring(param.length + 1)
    }

    return expr
  }

  /**
   * Normalize comparison operators
   * @private
   */
  static #normalizeOperator(op) {
    const mapping = {
      '===': '=',
      '==': '=',
      '!==': '!=',
      '!=': '!='
    }

    return mapping[op] || op
  }

  /**
   * Parse value from string
   * @private
   */
  static #parseValue(value) {
    // Try to parse as JSON
    try {
      return JSON.parse(value)
    } catch {
      // If it's a string literal
      if (value.startsWith("'") || value.startsWith('"')) {
        return value.slice(1, -1)
      }

      return value
    }
  }

  /**
   * Parse object literal
   * @private
   */
  static #parseObjectLiteral(str, param) {
    // Parse { userId: msg.payload.userId, amount: msg.payload.amount }
    const fields = {}

    // Simple regex-based parser
    const fieldRegex = /(\w+)\s*:\s*([^,}]+)/g
    let match

    while ((match = fieldRegex.exec(str)) !== null) {
      const [, key, value] = match
      fields[key] = this.#extractField(value.trim(), param)
    }

    return fields
  }
}

