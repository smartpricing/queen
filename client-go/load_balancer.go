package queen

import (
	"fmt"
	"hash/fnv"
	"sort"
	"sync"
	"time"
)

// LoadBalancerStrategy represents the load balancing strategy.
type LoadBalancerStrategy string

const (
	// StrategyRoundRobin cycles through healthy servers.
	StrategyRoundRobin LoadBalancerStrategy = "round-robin"
	// StrategySession sticks to the same server per session key.
	StrategySession LoadBalancerStrategy = "session"
	// StrategyAffinity uses consistent hashing with virtual nodes.
	StrategyAffinity LoadBalancerStrategy = "affinity"
)

// LoadBalancer manages server selection and health tracking.
type LoadBalancer struct {
	urls                   []string
	strategy               LoadBalancerStrategy
	affinityHashRing       int
	healthRetryAfterMillis int
	enableFailover         bool

	mu            sync.RWMutex
	healthStatus  map[string]*serverHealth
	roundRobinIdx int
	sessionMap    map[string]string
	virtualNodes  []virtualNode
}

// serverHealth tracks the health status of a server.
type serverHealth struct {
	healthy       bool
	lastFailure   time.Time
	failureCount  int
}

// virtualNode represents a virtual node in the consistent hash ring.
type virtualNode struct {
	hash uint32
	url  string
}

// NewLoadBalancer creates a new load balancer.
func NewLoadBalancer(urls []string, strategy LoadBalancerStrategy, affinityHashRing int, healthRetryAfterMillis int, enableFailover bool) *LoadBalancer {
	lb := &LoadBalancer{
		urls:                   urls,
		strategy:               strategy,
		affinityHashRing:       affinityHashRing,
		healthRetryAfterMillis: healthRetryAfterMillis,
		enableFailover:         enableFailover,
		healthStatus:           make(map[string]*serverHealth),
		sessionMap:             make(map[string]string),
	}

	// Initialize health status for all URLs
	for _, url := range urls {
		lb.healthStatus[url] = &serverHealth{healthy: true}
	}

	// Build virtual node ring for affinity strategy
	if strategy == StrategyAffinity {
		lb.buildVirtualNodeRing()
	}

	return lb
}

// buildVirtualNodeRing builds the consistent hash ring with virtual nodes.
func (lb *LoadBalancer) buildVirtualNodeRing() {
	lb.virtualNodes = nil

	for _, url := range lb.urls {
		health := lb.healthStatus[url]
		if health != nil && !health.healthy && !lb.shouldRetryUnhealthy(health) {
			continue // Skip unhealthy servers
		}

		// Add virtual nodes for this server
		for i := 0; i < lb.affinityHashRing; i++ {
			key := fmt.Sprintf("%s-%d", url, i)
			hash := fnvHash(key)
			lb.virtualNodes = append(lb.virtualNodes, virtualNode{
				hash: hash,
				url:  url,
			})
		}
	}

	// Sort by hash for binary search
	sort.Slice(lb.virtualNodes, func(i, j int) bool {
		return lb.virtualNodes[i].hash < lb.virtualNodes[j].hash
	})
}

// fnvHash computes FNV-1a hash of a string.
func fnvHash(s string) uint32 {
	h := fnv.New32a()
	h.Write([]byte(s))
	return h.Sum32()
}

// GetURL returns a URL based on the load balancing strategy.
func (lb *LoadBalancer) GetURL(affinityKey string) string {
	lb.mu.Lock()
	defer lb.mu.Unlock()

	switch lb.strategy {
	case StrategyAffinity:
		return lb.getAffinityURL(affinityKey)
	case StrategySession:
		return lb.getSessionURL(affinityKey)
	case StrategyRoundRobin:
		fallthrough
	default:
		return lb.getRoundRobinURL()
	}
}

// getAffinityURL returns a URL using consistent hashing.
func (lb *LoadBalancer) getAffinityURL(affinityKey string) string {
	if len(lb.virtualNodes) == 0 {
		// Fall back to round robin if no virtual nodes
		return lb.getRoundRobinURL()
	}

	if affinityKey == "" {
		// No affinity key, use round robin
		return lb.getRoundRobinURL()
	}

	// Find the virtual node for this key
	hash := fnvHash(affinityKey)
	idx := sort.Search(len(lb.virtualNodes), func(i int) bool {
		return lb.virtualNodes[i].hash >= hash
	})

	// Wrap around if needed
	if idx >= len(lb.virtualNodes) {
		idx = 0
	}

	url := lb.virtualNodes[idx].url

	// Check if healthy
	health := lb.healthStatus[url]
	if health != nil && !health.healthy && !lb.shouldRetryUnhealthy(health) {
		// Try to find next healthy server in the ring
		for i := 1; i < len(lb.virtualNodes); i++ {
			nextIdx := (idx + i) % len(lb.virtualNodes)
			nextURL := lb.virtualNodes[nextIdx].url
			nextHealth := lb.healthStatus[nextURL]
			if nextHealth == nil || nextHealth.healthy || lb.shouldRetryUnhealthy(nextHealth) {
				return nextURL
			}
		}
		// All servers unhealthy, return original
	}

	return url
}

// getSessionURL returns a URL based on session stickiness.
func (lb *LoadBalancer) getSessionURL(sessionKey string) string {
	if sessionKey == "" {
		return lb.getRoundRobinURL()
	}

	// Check if we have a session mapping
	if url, ok := lb.sessionMap[sessionKey]; ok {
		health := lb.healthStatus[url]
		if health == nil || health.healthy || lb.shouldRetryUnhealthy(health) {
			return url
		}
		// Session server is unhealthy, reassign
		delete(lb.sessionMap, sessionKey)
	}

	// Assign a new server
	url := lb.getRoundRobinURL()
	lb.sessionMap[sessionKey] = url
	return url
}

// getRoundRobinURL returns the next URL in round-robin order.
func (lb *LoadBalancer) getRoundRobinURL() string {
	if len(lb.urls) == 0 {
		return ""
	}

	// Try to find a healthy server
	healthyURLs := lb.getHealthyURLs()
	if len(healthyURLs) == 0 {
		// All servers unhealthy, return any
		lb.roundRobinIdx = (lb.roundRobinIdx + 1) % len(lb.urls)
		return lb.urls[lb.roundRobinIdx]
	}

	lb.roundRobinIdx = (lb.roundRobinIdx + 1) % len(healthyURLs)
	return healthyURLs[lb.roundRobinIdx]
}

// getHealthyURLs returns a list of healthy URLs.
func (lb *LoadBalancer) getHealthyURLs() []string {
	var healthy []string
	for _, url := range lb.urls {
		health := lb.healthStatus[url]
		if health == nil || health.healthy || lb.shouldRetryUnhealthy(health) {
			healthy = append(healthy, url)
		}
	}
	return healthy
}

// shouldRetryUnhealthy checks if enough time has passed to retry an unhealthy server.
func (lb *LoadBalancer) shouldRetryUnhealthy(health *serverHealth) bool {
	if health.healthy {
		return true
	}
	elapsed := time.Since(health.lastFailure)
	return elapsed.Milliseconds() >= int64(lb.healthRetryAfterMillis)
}

// MarkHealthy marks a URL as healthy.
func (lb *LoadBalancer) MarkHealthy(url string) {
	lb.mu.Lock()
	defer lb.mu.Unlock()

	health := lb.healthStatus[url]
	if health == nil {
		health = &serverHealth{}
		lb.healthStatus[url] = health
	}

	wasUnhealthy := !health.healthy
	health.healthy = true
	health.failureCount = 0

	// Rebuild ring if server was previously unhealthy
	if wasUnhealthy && lb.strategy == StrategyAffinity {
		lb.buildVirtualNodeRing()
	}

	logDebug("LoadBalancer.MarkHealthy", map[string]interface{}{
		"url": url,
	})
}

// MarkUnhealthy marks a URL as unhealthy.
func (lb *LoadBalancer) MarkUnhealthy(url string) {
	if !lb.enableFailover {
		return
	}

	lb.mu.Lock()
	defer lb.mu.Unlock()

	health := lb.healthStatus[url]
	if health == nil {
		health = &serverHealth{}
		lb.healthStatus[url] = health
	}

	wasHealthy := health.healthy
	health.healthy = false
	health.lastFailure = time.Now()
	health.failureCount++

	// Rebuild ring if server was previously healthy
	if wasHealthy && lb.strategy == StrategyAffinity {
		lb.buildVirtualNodeRing()
	}

	logWarn("LoadBalancer.MarkUnhealthy", map[string]interface{}{
		"url":          url,
		"failureCount": health.failureCount,
	})
}

// IsHealthy checks if a URL is healthy.
func (lb *LoadBalancer) IsHealthy(url string) bool {
	lb.mu.RLock()
	defer lb.mu.RUnlock()

	health := lb.healthStatus[url]
	if health == nil {
		return true
	}
	return health.healthy || lb.shouldRetryUnhealthy(health)
}

// GetAllURLs returns all configured URLs.
func (lb *LoadBalancer) GetAllURLs() []string {
	lb.mu.RLock()
	defer lb.mu.RUnlock()
	return append([]string{}, lb.urls...)
}

// Reset resets all health status and session mappings.
func (lb *LoadBalancer) Reset() {
	lb.mu.Lock()
	defer lb.mu.Unlock()

	for _, health := range lb.healthStatus {
		health.healthy = true
		health.failureCount = 0
	}
	lb.sessionMap = make(map[string]string)
	lb.roundRobinIdx = 0

	if lb.strategy == StrategyAffinity {
		lb.buildVirtualNodeRing()
	}
}
