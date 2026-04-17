// Tests for server-stamped producer identity (issue #23, feature A).
//
// These tests exercise two invariants:
//
//  1. The typed Message.ProducerSub field correctly receives the server-stamped
//     JWT sub claim on pop (the whole reason this client library needs a field
//     update at all - without it, json.Unmarshal silently drops the value).
//
//  2. The server's anti-impersonation invariant holds for Go clients: even if
//     a push request body contains producerSub, the server replaces it with
//     the authenticated JWT sub.
//
// The JWT-gated tests require the server to be running with JWT enabled and
// the matching JWT_SECRET env var to be set when running the tests.

package tests

import (
	"context"
	"crypto/hmac"
	"crypto/sha256"
	"encoding/base64"
	"encoding/json"
	"fmt"
	"net/http"
	"os"
	"strings"
	"testing"
	"time"
)

// ---------------------------------------------------------------------------
// Minimal HS256 JWT signer - keeps test deps at zero (pure stdlib).
// ---------------------------------------------------------------------------
func b64URL(b []byte) string {
	return strings.TrimRight(base64.URLEncoding.EncodeToString(b), "=")
}

func signHS256(payload map[string]interface{}, secret string) (string, error) {
	header, err := json.Marshal(map[string]string{"alg": "HS256", "typ": "JWT"})
	if err != nil {
		return "", err
	}
	payloadBytes, err := json.Marshal(payload)
	if err != nil {
		return "", err
	}
	encHeader := strings.ReplaceAll(strings.ReplaceAll(b64URL(header), "+", "-"), "/", "_")
	encPayload := strings.ReplaceAll(strings.ReplaceAll(b64URL(payloadBytes), "+", "-"), "/", "_")
	signingInput := encHeader + "." + encPayload
	mac := hmac.New(sha256.New, []byte(secret))
	mac.Write([]byte(signingInput))
	sig := mac.Sum(nil)
	encSig := strings.ReplaceAll(strings.ReplaceAll(b64URL(sig), "+", "-"), "/", "_")
	return signingInput + "." + encSig, nil
}

func makeJWT(t *testing.T, sub, secret string) string {
	t.Helper()
	now := time.Now().Unix()
	tok, err := signHS256(map[string]interface{}{
		"sub":      sub,
		"username": sub,
		"role":     "read-write",
		"iat":      now,
		"exp":      now + 3600,
	}, secret)
	if err != nil {
		t.Fatalf("signHS256: %v", err)
	}
	return tok
}

// ---------------------------------------------------------------------------
// DB helpers (ground-truth checks bypass the server so we can tell whether
// the server actually persisted the stamped value vs. just echoing it).
// ---------------------------------------------------------------------------
func getStoredProducerSub(ctx context.Context, t *testing.T, queueName, txID string) (*string, bool) {
	t.Helper()
	if dbPool == nil {
		t.Skip("dbPool not available; set PG_* env vars to run this test")
		return nil, false
	}

	var sub *string
	err := dbPool.QueryRow(ctx, `
		SELECT m.producer_sub
		FROM queen.messages m
		JOIN queen.partitions p ON p.id = m.partition_id
		JOIN queen.queues q ON q.id = p.queue_id
		WHERE q.name = $1 AND m.transaction_id = $2
	`, queueName, txID).Scan(&sub)
	if err != nil {
		t.Fatalf("query producer_sub: %v", err)
	}
	return sub, true
}

func httpPush(t *testing.T, queue, txID string, data map[string]interface{}, bearer string, extraFields map[string]interface{}) {
	t.Helper()
	item := map[string]interface{}{
		"queue":         queue,
		"partition":     "Default",
		"transactionId": txID,
		"payload":       data,
	}
	for k, v := range extraFields {
		item[k] = v
	}
	body, _ := json.Marshal(map[string]interface{}{"items": []interface{}{item}})

	req, err := http.NewRequest(http.MethodPost, serverURL+"/api/v1/push", strings.NewReader(string(body)))
	if err != nil {
		t.Fatalf("new request: %v", err)
	}
	req.Header.Set("Content-Type", "application/json")
	if bearer != "" {
		req.Header.Set("Authorization", "Bearer "+bearer)
	}
	res, err := http.DefaultClient.Do(req)
	if err != nil {
		t.Fatalf("push: %v", err)
	}
	defer res.Body.Close()
	if res.StatusCode >= 300 {
		t.Fatalf("push status %d", res.StatusCode)
	}
}

// ===========================================================================
// TEST: Message.ProducerSub is properly deserialised by the typed struct
// ---------------------------------------------------------------------------
// Pushes via HTTP with a fake producerSub in the body (which will be ignored),
// then pushes directly via the stored procedure (which accepts producerSub
// because it's the server's own interface to the SP), then verifies the Go
// client deserialises ProducerSub correctly on pop.
// ===========================================================================
func TestProducerSubFieldDeserialisation(t *testing.T) {
	client := requireClient(t)
	if dbPool == nil {
		t.Skip("dbPool not available")
	}
	ctx := context.Background()

	q := fmt.Sprintf("test-auth-go-field-%d", time.Now().UnixNano())
	txID := fmt.Sprintf("tx-go-field-%d", time.Now().UnixNano())

	// Use the SP directly to inject a known producer_sub (simulates a message
	// that was stamped at an earlier time by an authenticated JWT push).
	items, _ := json.Marshal([]map[string]interface{}{{
		"queue":         q,
		"partition":     "Default",
		"transactionId": txID,
		"payload":       map[string]interface{}{"hello": "world"},
		"producerSub":   "go-test-sub",
	}})
	if _, err := dbPool.Exec(ctx, "SELECT queen.push_messages_v2($1::jsonb)", string(items)); err != nil {
		t.Fatalf("push_messages_v2: %v", err)
	}

	msgs, err := client.Queue(q).Batch(10).Wait(false).Pop(ctx)
	if err != nil {
		t.Fatalf("pop: %v", err)
	}

	var found bool
	for _, m := range msgs {
		if m.TransactionID == txID {
			if m.ProducerSub != "go-test-sub" {
				t.Fatalf("ProducerSub = %q, want %q", m.ProducerSub, "go-test-sub")
			}
			found = true
			break
		}
	}
	if !found {
		t.Fatalf("did not find tx %s in %d popped messages", txID, len(msgs))
	}
}

// ===========================================================================
// TEST: Body-supplied producerSub is ignored (spoof prevention) - no auth
// ===========================================================================
func TestProducerSubBodyIgnoredWithoutAuth(t *testing.T) {
	requireClient(t)
	if dbPool == nil {
		t.Skip("dbPool not available")
	}
	if os.Getenv("JWT_SECRET") != "" {
		t.Skip("JWT_SECRET is set - run TestProducerSubStampedFromJwt instead")
	}
	ctx := context.Background()

	q := fmt.Sprintf("test-auth-go-noauth-%d", time.Now().UnixNano())
	txID := fmt.Sprintf("tx-go-noauth-%d", time.Now().UnixNano())

	httpPush(t, q, txID, map[string]interface{}{"hello": "world"}, "", map[string]interface{}{
		"producerSub": "attacker-no-jwt",
	})

	stored, ok := getStoredProducerSub(ctx, t, q, txID)
	if !ok {
		return
	}
	if stored != nil {
		t.Fatalf("expected NULL producer_sub (auth disabled) but got %q - client was able to set it!", *stored)
	}
}

// ===========================================================================
// TEST: Authenticated push stamps producer_sub from JWT sub claim
// ===========================================================================
func TestProducerSubStampedFromJwt(t *testing.T) {
	secret := os.Getenv("JWT_SECRET")
	if secret == "" {
		t.Skip("set JWT_SECRET (matching server) to run")
	}
	if dbPool == nil {
		t.Skip("dbPool not available")
	}
	ctx := context.Background()

	q := fmt.Sprintf("test-auth-go-jwt-%d", time.Now().UnixNano())
	txID := fmt.Sprintf("tx-go-jwt-%d", time.Now().UnixNano())
	token := makeJWT(t, "alice-go-producer", secret)

	// Queue configure (auth required under JWT).
	req, _ := http.NewRequest(http.MethodPost, serverURL+"/api/v1/configure",
		strings.NewReader(fmt.Sprintf(`{"queue":%q,"options":{}}`, q)))
	req.Header.Set("Content-Type", "application/json")
	req.Header.Set("Authorization", "Bearer "+token)
	_, _ = http.DefaultClient.Do(req)

	httpPush(t, q, txID, map[string]interface{}{"hello": "world"}, token, nil)

	stored, ok := getStoredProducerSub(ctx, t, q, txID)
	if !ok {
		return
	}
	if stored == nil || *stored != "alice-go-producer" {
		got := "(nil)"
		if stored != nil {
			got = *stored
		}
		t.Fatalf("producer_sub = %q, want %q", got, "alice-go-producer")
	}
}

// ===========================================================================
// TEST: Spoofing is blocked even with a valid JWT
// ===========================================================================
func TestProducerSubSpoofingIgnoredWithJwt(t *testing.T) {
	secret := os.Getenv("JWT_SECRET")
	if secret == "" {
		t.Skip("set JWT_SECRET (matching server) to run")
	}
	if dbPool == nil {
		t.Skip("dbPool not available")
	}
	ctx := context.Background()

	q := fmt.Sprintf("test-auth-go-spoof-%d", time.Now().UnixNano())
	txID := fmt.Sprintf("tx-go-spoof-%d", time.Now().UnixNano())
	token := makeJWT(t, "legit-go-producer", secret)

	req, _ := http.NewRequest(http.MethodPost, serverURL+"/api/v1/configure",
		strings.NewReader(fmt.Sprintf(`{"queue":%q,"options":{}}`, q)))
	req.Header.Set("Content-Type", "application/json")
	req.Header.Set("Authorization", "Bearer "+token)
	_, _ = http.DefaultClient.Do(req)

	httpPush(t, q, txID, map[string]interface{}{"hello": "world"}, token, map[string]interface{}{
		"producerSub": "attacker",
	})

	stored, ok := getStoredProducerSub(ctx, t, q, txID)
	if !ok {
		return
	}
	if stored == nil || *stored != "legit-go-producer" {
		got := "(nil)"
		if stored != nil {
			got = *stored
		}
		t.Fatalf("impersonation not prevented: stored %q, want %q", got, "legit-go-producer")
	}
}
