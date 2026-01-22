# Long-Running Session Best Practices

Author: Puneeth R  
Version: 1.4.0

## Introduction

OmniSync has been battle-tested for long-running sessions (24+ hours). This guide shares practical lessons learned and best practices for production deployments.

## Memory Management

### 1. Enable Auto-GC from the Start

Don't wait for memory issues to appear. Configure GC upfront:

```cpp
Sequence::GCConfig gc_config;
gc_config.auto_gc_enabled = true;
gc_config.tombstone_threshold = 1000;    // Adjust based on your workload
gc_config.min_age_threshold = 100;       // Conservative default

doc.setGCConfig(gc_config);
```

**Tuning tips:**
- Lower threshold = more frequent GC = lower memory usage
- Higher threshold = less GC overhead = faster operations
- Start conservative (1000), profile, then adjust

### 2. Monitor Memory Periodically

```cpp
// Every 5-10 minutes
MemoryStats stats = doc.getMemoryStats();

if (stats.total_bytes() > threshold) {
    log_warning("Memory usage: " + std::to_string(stats.total_bytes() / 1024) + " KB");
    
    // Force GC if needed
    doc.garbageCollectLocal(50);
}
```

### 3. Watch Orphan Buffers

Orphans can accumulate if network is unstable:

```cpp
Sequence::OrphanConfig orphan_config;
orphan_config.max_orphan_count = 1000;
orphan_config.max_orphan_age = 300;  // 5 minutes

doc.setOrphanConfig(orphan_config);

// Monitor
if (doc.getOrphanBufferSize() > 500) {
    log_warning("High orphan count - possible network issues");
}
```

## Distributed GC Coordination

### 1. Set Realistic Timeouts

For distributed systems, peer timeouts matter:

```cpp
GCCoordinator::Config config;
config.peer_timeout_ms = 60000;  // 60 seconds for WAN
// config.peer_timeout_ms = 10000;  // 10 seconds for LAN

config.heartbeat_interval_ms = config.peer_timeout_ms / 3;  // Rule of thumb
```

### 2. Handle Network Partitions

Peers that timeout are excluded from GC frontier:

```cpp
// Periodically check peer health
std::vector<uint64_t> active = gc_coord.getActivePeers();

if (active.size() < expected_peer_count) {
    log_info("Some peers are inactive - GC will be conservative");
}
```

### 3. Balance GC Frequency

```cpp
// For high-traffic systems
config.gc_interval_ms = 300000;  // 5 minutes

// For low-traffic systems  
config.gc_interval_ms = 60000;   // 1 minute
```

## Performance Monitoring

### 1. Track GC Performance

```cpp
MemoryStats stats = doc.getMemoryStats();

std::cout << "GC Stats:\n"
          << "  Total runs: " << stats.gc_stats.total_gc_runs << "\n"
          << "  Avg time: " << stats.gc_stats.avg_gc_time_us << " μs\n"
          << "  Peak time: " << stats.gc_stats.max_gc_time_us << " μs\n";

// Alert if GC is taking too long
if (stats.gc_stats.max_gc_time_us > 1000) {  // 1ms
    log_warning("GC taking longer than expected");
}
```

### 2. Operation Rate Limits

Don't overwhelm the system:

```cpp
// Maintain sustainable operation rate
const size_t MAX_OPS_PER_SECOND = 100;

// Use rate limiting or throttling as needed
```

### 3. Periodic Convergence Checks

In multi-user scenarios, verify convergence:

```cpp
// Every hour or after major activity
std::string my_content = doc.toString();
std::string peer_content = /* fetch from peer */;

if (my_content != peer_content) {
    log_error("DIVERGENCE DETECTED");
    // Trigger reconciliation protocol
}
```

## Network Recommendations

### 1. Delta Sync is Critical

Always use delta sync to minimize bandwidth:

```cpp
// Get only new operations since last sync
DeltaState delta = doc.getDelta(peer_vector_clock);

// Send compressed
std::vector<uint8_t> compressed = compressDelta(delta);
network_send(compressed);
```

### 2. Batch Updates When Possible

```cpp
// Instead of sending every operation immediately:
std::vector<Atom> pending_ops;

// Accumulate for 100ms
pending_ops.push_back(new_atom);

// Then send in batch
if (time_since_last_send > 100ms || pending_ops.size() > 10) {
    send_batch(pending_ops);
    pending_ops.clear();
}
```

### 3. Handle Offline Gracefully

```cpp
// Save state before going offline
if (network_disconnected) {
    doc.save(backup_file);
}

// Restore and sync when back online
if (network_reconnected) {
    doc.load(backup_file);
    sync_with_peers();
}
```

## Common Pitfalls

### 1. Forgetting to Update Peer States

```cpp
// WRONG: Merging without updating coordinator
doc.remoteMerge(atom);

// RIGHT: Always update coordinator
doc.remoteMerge(atom);
gc_coord.updatePeerState(peer_id, their_vector_clock);
```

### 2. Aggressive Local GC in Distributed Systems

```cpp
// WRONG: Local GC without considering peers
doc.garbageCollectLocal(10);  // Might break convergence!

// RIGHT: Use coordinated GC
gc_coord.performCoordinatedGC(doc);
```

### 3. Ignoring Memory Stats

Monitor at least once per hour:

```cpp
void periodic_health_check() {
    MemoryStats stats = doc.getMemoryStats();
    
    log_info("Health check: " +
             std::to_string(stats.atom_count) + " atoms, " +
             std::to_string(stats.tombstone_count) + " tombstones, " +
             std::to_string(stats.total_bytes() / 1024) + " KB");
}
```

## Production Checklist

Before deploying for long-running sessions:

- [ ] Auto-GC enabled and tuned
- [ ] Orphan buffer limits configured
- [ ] GC coordination setup (if distributed)
- [ ] Memory monitoring in place
- [ ] Periodic convergence checks (if multi-user)
- [ ] Network error handling tested
- [ ] Save/load mechanisms working
- [ ] Logging and alerting configured

## Observed Behavior (24-Hour Test)

From our stability testing:

**Memory**: Stays bounded (0 → 109 KB over 24 hours)
**Convergence**: 100% maintained across all peers
**GC**: Runs automatically, keeps tombstone count stable
**Performance**: No degradation over time

**Key insight**: The system is stable. Trust the auto-GC. Monitor, but don't micromanage.

## When Things Go Wrong

### High Memory Usage

1. Check tombstone count: `doc.getTombstoneCount()`
2. Check orphan count: `doc.getOrphanBufferSize()`  
3. Force GC if auto-GC disabled: `doc.garbageCollectLocal(100)`
4. Review GC configuration

### Divergence Issues

1. Verify all operations are being broadcast
2. Check that peer states are being updated
3. Ensure GC is coordinated (not local-only)
4. Review network logs for dropped messages

### Performance Degradation

1. Check GC stats for unusually long GC times
2. Profile hot paths (insertion, merge)
3. Consider reducing operation rate
4. Review memory stats for unexpected growth

---

For more information, see:
- [GC_COORDINATION_GUIDE.md](GC_COORDINATION_GUIDE.md)
- [STABILITY_TESTING_GUIDE.md](STABILITY_TESTING_GUIDE.md)
- Test suite: `tests/stability_test.cpp`
