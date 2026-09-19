/*
** Copyright (C) 2007-2019 SWGEmu
** See file COPYING for copying conditions.
*/

#include "engine/orb/DistributedObjectBroker.h"

DistributedObjectServant::DistributedObjectServant() {
}

DistributedObjectServant::~DistributedObjectServant() {
	// UAF failsafe (prod 2026-09-07/09-12/09-19): a servant can be released to
	// refcount 0 and destroyed while its stub is still alive and externally
	// referenced (a strong ManagedReference pins the STUB, never this impl).
	// The stub's _impl would then dangle non-null and the next forwarder virtual
	// dispatch (notifyDissapear/insertToMessage/onContainerLoaded/writeObject)
	// would jump through freed memory. WeakReference::get() returns empty when the
	// stub is itself finalizing, so on the normal teardown path this is a no-op.
	Reference<DistributedObjectStub*> stub = _stub.get();

	if (stub != nullptr && stub->_getImplementationForRead() == this) {
		// Anomaly: dying under a live stub that still points at us. Log the
		// release stack (rare by construction) to localize the true unmatched
		// release, then defuse the UAF.
		StackTrace trace;

		Logger::console.error("DistributedObjectServant 0x"
			+ String::hexvalueOf((int64) stub->_getObjectID())
			+ " destroyed under a live stub with dangling _impl -- clearing to prevent UAF");
		trace.print();

		stub->_clearDeadImplementation(this);
	}
}

void DistributedObjectServant::setDeployingName(const String& name) {
	_stub.get()->_setName(name);
}

String DistributedObjectServant::toString() const {
	return _stub.get()->_getName();
}
