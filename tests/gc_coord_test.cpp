#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>
#include "omnisync/omnisync.hpp"
using namespace omnisync::core;

static void test_quorum_and_frontier() {
    GCCoordinator::Config cfg; cfg.min_peers_for_gc=2; cfg.gc_interval_ms=0;
    GCCoordinator gc(1,cfg); gc.registerPeer(2); gc.registerPeer(3);
    VectorClock me(1), p2(2), p3(3); me.update(1,5); p2.update(1,4); p3.update(1,3);
    gc.updateMyVectorClock(me); gc.updatePeerState(2,p2); assert(!gc.hasCompleteQuorum());
    gc.updatePeerState(3,p3); assert(gc.hasCompleteQuorum());
    assert(gc.computeStableFrontier().get(1)==3);
}

static void test_timeout_does_not_authorize_gc() {
    GCCoordinator::Config cfg; cfg.peer_timeout_ms=1; cfg.min_peers_for_gc=1;
    GCCoordinator gc(1,cfg); gc.registerPeer(2); VectorClock v(2); v.update(2,9); gc.updatePeerState(2,v);
    std::this_thread::sleep_for(std::chrono::milliseconds(3));
    // Timeout is telemetry only; peer remains part of quorum.
    assert(gc.getActivePeerCount()==1);
    assert(gc.hasCompleteQuorum());
}

static void test_retire_requires_explicit_reactivation() {
    GCCoordinator gc(1); gc.registerPeer(2); VectorClock v(2); v.update(2,5); gc.updatePeerState(2,v); gc.retirePeer(2);
    assert(gc.getActivePeerCount()==0); assert(gc.getKnownPeers().empty());
    gc.processHeartbeat(2,v); assert(gc.getActivePeerCount()==0);
    gc.reRegisterPeer(2); assert(gc.getActivePeerCount()==0); assert(!gc.hasCompleteQuorum());
    gc.updatePeerState(2,v); assert(gc.getActivePeerCount()==1); assert(gc.hasCompleteQuorum());
}

static void test_sequence_gc_safety() {
    Sequence doc(1); Atom a=doc.localInsert(0,'x'); doc.localDeleteId(a.id); VectorClock f(1); f.update(1,2);
    GCCoordinator gc(1); assert(gc.performCoordinatedGC(doc)==0); assert(doc.hasAtom(a.id));
}

int main(){test_quorum_and_frontier();test_timeout_does_not_authorize_gc();test_retire_requires_explicit_reactivation();test_sequence_gc_safety();std::cout<<"gc_coord_test: PASS\n";}
