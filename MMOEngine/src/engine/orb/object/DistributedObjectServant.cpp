/*
** Copyright (C) 2007-2019 SWGEmu
** See file COPYING for copying conditions.
*/

#include "engine/orb/DistributedObjectBroker.h"

DistributedObjectServant::DistributedObjectServant() {
}

DistributedObjectServant::~DistributedObjectServant() {
	// DETECTOR for the dangling-_impl UAF family (prod 2026-09-07/09-12/09-19): a
	// servant can be released to refcount 0 and destroyed while its stub is still
	// alive and externally referenced (a strong ManagedReference pins the STUB,
	// never this impl). The stub's _impl then dangles non-null and the next
	// forwarder virtual dispatch (notifyDissapear/insertToMessage/
	// onContainerLoaded/writeObject) jumps through freed memory.
	// WeakReference::get() returns empty when the stub is itself finalizing, so on
	// the normal teardown path this is a no-op.
	//
	// We deliberately do NOT null the stub's _impl here. Doing so leaves the stub
	// at deployed == true with _impl == nullptr, which the forwarders do not
	// handle: ManagedObjectImplementation::wlock takes the real pthread rwlock
	// BEFORE testing _impl, then __wlock sees deployed == true, builds an RPC and
	// throws -- and Locker's constructor throws after the lock is taken but before
	// ~Locker exists, so the write lock leaks permanently and every later thread
	// blocks forever on that object. Worse, the unlock path throws from inside
	// ~Locker, which is implicitly noexcept, and aborts the process. A silent,
	// unrecoverable wedge is strictly worse than the existing crash + watchdog
	// restart, so we only report. Fix the true unmatched release this points at.
	Reference<DistributedObjectStub*> stub = _stub.get();

	if (stub != nullptr && stub->_getImplementationForRead() == this) {
		// Anomaly: dying under a live stub that still points at us. Log the
		// release stack (rare by construction) to localize the real bug.
		StackTrace trace;

		Logger::console.error("DistributedObjectServant 0x"
			+ String::hexvalueOf((int64) stub->_getObjectID())
			+ " destroyed under a live stub with dangling _impl -- a UAF may follow;"
			  " the stack below is the unmatched release to fix");
		trace.print();
	}
}

void DistributedObjectServant::setDeployingName(const String& name) {
	_stub.get()->_setName(name);
}

String DistributedObjectServant::toString() const {
	return _stub.get()->_getName();
}
