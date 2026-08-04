// Yjs benchmark — mirrors omnisync_bench.cpp exactly
// Run: node --expose-gc yjs_bench.mjs
import * as Y from 'yjs';

const N       = 10000;
const REPEATS = 5;
const SEED    = 42;

// ── seeded pseudo-random (same sequence as srand(42) + rand() % n) ─────────
// Implements the C stdlib LCG so positions match exactly
let lcg_state = SEED;
function lcg_rand() {
    lcg_state = (Math.imul(1103515245, lcg_state) + 12345) & 0x7fffffff;
    return lcg_state;
}
function seeded_rand_mod(n) { return lcg_rand() % n; }

function median(arr) {
    const s = [...arr].sort((a, b) => a - b);
    const m = Math.floor(s.length / 2);
    return s.length % 2 === 0 ? (s[m-1] + s[m]) / 2 : s[m];
}

function timeMs(fn) {
    const t0 = performance.now();
    fn();
    return performance.now() - t0;
}

// ── Workload 1: Sequential append ────────────────────────────────────────────
function bench_sequential_insert() {
    const times = [];
    for (let r = 0; r < REPEATS; r++) {
        times.push(timeMs(() => {
            const doc = new Y.Doc();
            const text = doc.getText('content');
            doc.transact(() => {
                for (let i = 0; i < N; i++)
                    text.insert(i, String.fromCharCode(97 + (i % 26)));
            });
        }));
    }
    return median(times);
}

// ── Workload 2: Random position insert ───────────────────────────────────────
function bench_random_insert() {
    // Pre-generate positions (same logic as C++ code)
    lcg_state = SEED;
    const positions = [];
    for (let i = 0; i < N; i++)
        positions.push(i === 0 ? 0 : seeded_rand_mod(i + 1));

    const times = [];
    for (let r = 0; r < REPEATS; r++) {
        times.push(timeMs(() => {
            const doc = new Y.Doc();
            const text = doc.getText('content');
            doc.transact(() => {
                for (let i = 0; i < N; i++)
                    text.insert(positions[i], String.fromCharCode(97 + (i % 26)));
            });
        }));
    }
    return median(times);
}

// ── Workload 3: Sequential delete from front ─────────────────────────────────
function bench_sequential_delete() {
    // Build base document
    const base = new Y.Doc();
    const btext = base.getText('content');
    btext.insert(0, 'a'.repeat(N));

    const times = [];
    for (let r = 0; r < REPEATS; r++) {
        // Clone doc via snapshot
        const doc = new Y.Doc();
        Y.applyUpdate(doc, Y.encodeStateAsUpdate(base));
        const text = doc.getText('content');
        times.push(timeMs(() => {
            doc.transact(() => {
                for (let i = 0; i < N; i++)
                    text.delete(0, 1);
            });
        }));
    }
    return median(times);
}

// ── Workload 4: Concurrent merge ─────────────────────────────────────────────
function bench_concurrent_merge() {
    // Generate ops for Alice and Bob offline
    const alice = new Y.Doc({ guid: 'alice' });
    const bob   = new Y.Doc({ guid: 'bob'   });
    const at = alice.getText('content');
    const bt = bob.getText('content');

    const aliceUpdates = [];
    const bobUpdates   = [];

    alice.on('update', (u) => aliceUpdates.push(u));
    bob.on('update',   (u) => bobUpdates.push(u));

    alice.transact(() => {
        for (let i = 0; i < N/2; i++)
            at.insert(i, String.fromCharCode(97 + (i % 26)));
    });
    bob.transact(() => {
        for (let i = 0; i < N/2; i++)
            bt.insert(i, String.fromCharCode(65 + (i % 26)));
    });

    alice.off('update');
    bob.off('update');

    const aliceState = Y.encodeStateAsUpdate(alice);
    const bobState   = Y.encodeStateAsUpdate(bob);

    const times = [];
    for (let r = 0; r < REPEATS; r++) {
        times.push(timeMs(() => {
            const a2 = new Y.Doc();
            Y.applyUpdate(a2, aliceState);
            Y.applyUpdate(a2, bobState);   // merge bob into alice

            const b2 = new Y.Doc();
            Y.applyUpdate(b2, bobState);
            Y.applyUpdate(b2, aliceState); // merge alice into bob
        }));
    }
    return median(times);
}

// ── Workload 5: toString / snapshot ──────────────────────────────────────────
function bench_to_string() {
    const doc = new Y.Doc();
    const text = doc.getText('content');
    text.insert(0, 'a'.repeat(N));

    const SNAP_REPS = 1000;
    const times = [];
    for (let r = 0; r < REPEATS; r++) {
        times.push(timeMs(() => {
            let sink = 0;
            for (let i = 0; i < SNAP_REPS; i++)
                sink += text.toString().length;
        }));
    }
    return median(times);
}

// ── main ─────────────────────────────────────────────────────────────────────
console.log(`\n[Yjs Node.js Benchmark] N=${N} ops, ${REPEATS} runs, median reported\n`);

const r1 = bench_sequential_insert();
const r2 = bench_random_insert();
const r3 = bench_sequential_delete();
const r4 = bench_concurrent_merge();
const r5 = bench_to_string();

console.log(`RESULT:sequential_insert:${r1.toFixed(3)}`);
console.log(`RESULT:random_insert:${r2.toFixed(3)}`);
console.log(`RESULT:sequential_delete:${r3.toFixed(3)}`);
console.log(`RESULT:concurrent_merge:${r4.toFixed(3)}`);
console.log(`RESULT:tostring_snapshot:${r5.toFixed(3)}`);
