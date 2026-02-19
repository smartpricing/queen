# Message Encryption

Queen MQ supports **AES-256-GCM** encryption of message payloads at rest. Encryption is opt-in per queue and requires a server-side key configured via an environment variable.

::: danger Encryption is silently disabled without the key
If `QUEEN_ENCRYPTION_KEY` is not set and a queue has `encryptionEnabled: true`, messages are **stored as plaintext** — no error is returned to the client, only a warning is logged on the server. Always verify the key is present before enabling encryption on a queue.
:::

---

## How it works

- Each message payload is encrypted individually with AES-256-GCM
- A **random 16-byte IV** is generated per message — no two messages share the same IV
- The GCM **authentication tag** (16 bytes) is stored alongside the ciphertext, providing both confidentiality and integrity
- Encrypted payloads are stored in the database as `{ encrypted, iv, authTag }` (all base64-encoded) instead of raw JSON
- The server transparently decrypts on POP — clients always receive the original plaintext payload

---

## Step 1 — Generate the key

The key must be exactly **32 bytes, encoded as 64 hexadecimal characters**.

```bash
# Generate a cryptographically secure random key
openssl rand -hex 32
```

Example output:
```
a3f8c2d1e4b7096f5a2c8d3e1f4b7a90c2d5e8f1a4b7c0d3e6f9a2b5c8d1e4f7
```

::: warning Store the key securely
Losing the key means losing access to all encrypted messages. Store it in a secrets manager (Kubernetes Secret, AWS Secrets Manager, HashiCorp Vault, etc.) — never commit it to source control.
:::

---

## Step 2 — Configure the server

Set the environment variable before starting Queen:

```bash
export QUEEN_ENCRYPTION_KEY=a3f8c2d1e4b7096f5a2c8d3e1f4b7a90c2d5e8f1a4b7c0d3e6f9a2b5c8d1e4f7
./bin/queen-server
```

On startup, the server logs:
```
[info] Encryption service initialized (AES-256-GCM)
```

If the variable is missing or invalid, the server logs:
```
[warn] Encryption disabled - QUEEN_ENCRYPTION_KEY not set
```
and all queues with `encryptionEnabled: true` will silently store plaintext.

### Docker

```bash
docker run -p 6632:6632 \
  -e PG_HOST=postgres \
  -e QUEEN_ENCRYPTION_KEY=a3f8c2d1e4b7096f5a2c8d3e1f4b7a90c2d5e8f1a4b7c0d3e6f9a2b5c8d1e4f7 \
  smartnessai/queen-mq:{{VERSION}}
```

### Kubernetes

Create a Secret and reference it from the StatefulSet:

```bash
kubectl create secret generic queen-encryption \
  --from-literal=QUEEN_ENCRYPTION_KEY=a3f8c2d1e4b7096f5a2c8d3e1f4b7a90c2d5e8f1a4b7c0d3e6f9a2b5c8d1e4f7 \
  -n your-namespace
```

```yaml
# In your StatefulSet spec
envFrom:
  - secretRef:
      name: queen-encryption
```
