# Queen C++ Server Configuration Guide

## ⚠️ **CRITICAL: Database Pool Size**

### **The Problem:**
```
Error: mutex lock failed: Invalid argument
```

This occurs when **DB_POOL_SIZE ≤ Thread Count**, causing connection exhaustion.

### **The Solution:**

```bash
# ❌ WRONG - Will cause connection errors
DB_POOL_SIZE=20 ./bin/queen-server  # Same as thread count

# ✅ CORRECT - Stable operation
DB_POOL_SIZE=50 ./bin/queen-server  # 2.5x thread count
```

### **Why This Matters:**

```
20 worker threads + 20 DB connections = EXHAUSTION
────────────────────────────────────────────────
All 20 threads busy with ACK (~600ms each)
New request arrives → No free connection
PostgreSQL closes idle connection → "mutex lock failed"
```

With `DB_POOL_SIZE=50`:
- 20 threads active
- 30 connections available
- **No waiting, no exhaustion** ✅

---

## 🚀 Recommended Configurations

### **Development / Testing:**
```bash
DB_POOL_SIZE=50 \
QUEEN_ENCRYPTION_KEY=2e433dbbc61b88406530f4613ddd9ea4e5b575364029587ee829fbe285f8dbbc \
./bin/queen-server
```

### **Production (High Load):**
```bash
DB_POOL_SIZE=100 \
QUEEN_ENCRYPTION_KEY=your_production_key \
./bin/queen-server
```

### **Production (Very High Load):**
```bash
# Increase thread count in code to 50, then:
DB_POOL_SIZE=150 \
QUEEN_ENCRYPTION_KEY=your_production_key \
./bin/queen-server
```

---

## 📊 Batch Size Recommendations

### **Optimal Performance:**
- **Batch 1000**: Best balance of throughput and stability
- **Throughput**: ~120,000 msg/s

### **Large Batches:**
- **Batch 5000**: Supported, ~5-10% slower
- **Batch 10000**: Works but may cause memory pressure

### **Memory Usage by Batch Size:**

| Batch Size | Memory per Request | 10 Concurrent | Recommended Pool |
|------------|-------------------|---------------|------------------|
| 1,000 | ~3 MB | ~30 MB | DB_POOL_SIZE=50 |
| 5,000 | ~15 MB | ~150 MB | DB_POOL_SIZE=100 |
| 10,000 | ~30 MB | ~300 MB | DB_POOL_SIZE=150 |

---

## 🔧 Thread Pool Configuration

Currently hardcoded in `server.cpp` line 57:

```cpp
int thread_count = std::min(20, static_cast<int>(std::thread::hardware_concurrency()));
```

To change:
1. Edit `src/server.cpp` line 57
2. Recompile: `cd src-cpp && make bin/queen-server`
3. Adjust `DB_POOL_SIZE` to 2.5x new thread count

---

## 🎯 Performance Tuning

### **For Maximum Throughput:**
```bash
# 50 threads for massive parallelism
# Edit server.cpp: thread_count = 50
DB_POOL_SIZE=150 ./bin/queen-server
```

### **For Stability:**
```bash
# 10 threads, more conservative
# Edit server.cpp: thread_count = 10  
DB_POOL_SIZE=30 ./bin/queen-server
```

### **For Memory Efficiency:**
```bash
# 5 threads, minimal memory
# Edit server.cpp: thread_count = 5
DB_POOL_SIZE=15 ./bin/queen-server
```

---

## ⚡ Benchmark Results by Configuration

### **DB_POOL_SIZE=20, Threads=20, Batch=1000:**
- ❌ **Connection errors after ~30 batches**
- Throughput: ~2,000 msg/s (before failure)

### **DB_POOL_SIZE=50, Threads=20, Batch=1000:**
- ✅ **Stable, zero errors**
- Throughput: **121,359 msg/s**
- Peak: 147,929 msg/s

### **DB_POOL_SIZE=100, Threads=50, Batch=1000:**
- ✅ **Maximum performance**
- Expected: **250,000+ msg/s**

---

## 📝 Quick Start

```bash
# 1. Build
cd src-cpp && make

# 2. Run with recommended settings
cd ..
DB_POOL_SIZE=50 \
QUEEN_ENCRYPTION_KEY=2e433dbbc61b88406530f4613ddd9ea4e5b575364029587ee829fbe285f8dbbc \
./src-cpp/bin/queen-server

# 3. Benchmark
node src/benchmark/producer.js
node src/benchmark/consumer.js
```

Expected output:
```
Average Throughput: 121,359 msg/s
Peak: 147,929 msg/s
Zero connection errors ✅
```

---

## 🐛 Troubleshooting

### **"mutex lock failed: Invalid argument"**
**Cause:** `DB_POOL_SIZE` too small  
**Fix:** Increase to 50+

### **Server crashes on startup**
**Cause:** Race condition in ThreadPool init  
**Fix:** Retry 2-3 times (already improved in latest code)

### **Segfault on shutdown**
**Cause:** Worker threads accessing freed resources  
**Fix:** Wait 1-2 seconds after Ctrl+C (improved in latest code)

### **"terminated" with large batches**
**Cause:** Memory pressure with 10K+ message batches  
**Fix:** Use batch=1000 or increase system memory

---

## ✅ Verification

After starting server, check logs for:

```
✅ Thread pool initialized with 20 workers (DB pool: 50 connections)
```

If you see warning:
```
⚠️  Thread count (20) >= DB pool size (20) - may cause connection exhaustion!
⚠️  Recommend: DB_POOL_SIZE >= 50 (2x thread count)
```

→ **Restart with larger `DB_POOL_SIZE`**

---

**Key Takeaway:** Always set `DB_POOL_SIZE` to **at least 2.5x your thread count** for stable operation!

