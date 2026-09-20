/*
** Copyright (C) 2007-2019 SWGEmu
** See file COPYING for copying conditions.
*/

#ifndef DISTRIBUTEDOBJECTSTUB_H_
#define DISTRIBUTEDOBJECTSTUB_H_

#include "engine/util/Singleton.h"

#include "DistributedObject.h"
#include "DistributedObjectServant.h"

namespace engine {
  namespace ORB {

	class DistributedObjectStub : public DistributedObject {
	protected:
		Reference<DistributedObjectServant*> _impl;

		bool deployed;

		DistributedObjectClassHelper* _classHelper;

		bool destroyed;

	#ifdef TRACE_REFERENCING
		Vector<StackTrace*> traces;

		StackTrace* finalizedTrace;
	#endif

	public:
		DistributedObjectStub();

		~DistributedObjectStub();

		//virtual DistributedObjectStub* clone();

		// deployment methods
		void deploy();
		void deploy(const char* name);
		void deploy(const String& name);
		void deploy(const String& name, sys::uint64 nid);

		void _requestServant();

		bool undeploy();

		// reference counting methods
		virtual void finalize();

		//virtual void acquire();

		//virtual void release();

		void printReferenceTrace() const;

		// setters
		inline void setDeployingName(const String& name) {
			_setName(name);
		}

		inline void _setClassHelper(DistributedObjectClassHelper* helper) {
			_classHelper = helper;
		}

		inline void setDeployed(bool val) {
			deployed = val;
		}

		virtual void _setImplementation(DistributedObjectServant* impl) {
			_impl = impl;
		}

		// ⛔ NOT CALLED, AND MUST NOT BE CALLED AS-IS. Kept only to document the
		// attempted mitigation for the dangling-_impl UAF family (prod 2026-09-07/
		// 09-12/09-19) and to make re-enabling it a one-line change once the
		// precondition below is met.
		//
		// The intent was: if a servant is destroyed while this stub still points
		// _impl at it, raw-null _impl so the forwarders' (_impl == NULL) guard stops
		// virtual-dispatching through freed memory. initializeWithoutAcquire is a
		// raw store so it does not release the already-dying servant and underflow
		// its zero strong count, and it is non-virtual so it cannot shift vtable
		// indices under the box's incremental-only GCC14 builds.
		//
		// Why it is disabled: it leaves the stub at deployed == true with
		// _impl == nullptr, which no forwarder handles.
		// ManagedObjectImplementation::wlock takes the real pthread rwlock BEFORE
		// testing _impl; __wlock then sees deployed == true, builds an RPC and
		// throws; Locker's ctor throws after the lock is held but before ~Locker
		// exists, so the write lock is leaked permanently and every later thread
		// blocks on that object. The unlock path throws from inside ~Locker, which
		// is implicitly noexcept, and terminates the process. Clearing `deployed`
		// does not help -- the !deployed branch throws too.
		//
		// PRECONDITION to re-enable: make the lock forwarders test _impl BEFORE
		// acquiring the rwlock (and return/throw a catchable ObjectNotLocalException
		// without holding it). Until then, the destructor only reports.
		void _clearDeadImplementation(DistributedObjectServant* dying) {
			if (_getImplementationForRead() == dying)
				_impl.initializeWithoutAcquire(nullptr);
		}

		// getters
		inline bool isDeployed() const {
			return deployed;
		}

		virtual DistributedObjectServant* _getImplementation() {
			return _impl;
		}

		virtual DistributedObjectServant* _getImplementationForRead() const {
			return _impl;
		}

		inline DistributedObjectClassHelper* _getClassHelper() {
			return _classHelper;
		}

		virtual DistributedObjectServant* getServant() {
			return _impl;
		}
	};

	class DummyConstructorParameter : public Singleton<DummyConstructorParameter>, public Object {
		friend class SingletonWrapper<DummyConstructorParameter>;
	};

  } // namespace ORB
} // namespace engine

using namespace engine::ORB;

#endif /*DISTRIBUTEDOBJECTSTUB_H_*/
