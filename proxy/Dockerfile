FROM node:22-alpine

WORKDIR /app

# Copy package files
COPY package*.json ./

# Install production dependencies
RUN npm ci --only=production

# Copy application code
COPY src ./src
COPY public ./public

# Expose proxy port
EXPOSE 3000

# Run the proxy server
CMD ["node", "src/server.js"]

