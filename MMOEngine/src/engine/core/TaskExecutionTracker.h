/*
** Copyright (C) 2007-2019 SWGEmu
** See file COPYING for copying conditions.
*/

#ifndef ENGINE_CORE_TASKEXECUTIONTRACKER_H_
#define ENGINE_CORE_TASKEXECUTIONTRACKER_H_

#include "system/platform.h"

#include <chrono>
#include <mutex>

namespace engine {
  namespace core {

	class Task;

	// Outer Rim: where a task-running thread's current execution is spending its time, readable from another thread.
	// The save barrier waits on a thread's blockMutex, which that thread holds from taking a task until after the
	// task's final transaction commit. Task::lastElapsedTime stops before that commit, and a SAVE BARRIER SLOW line
	// only names the task, so a slow hold could not be split into dispatch, run, commit and the task's own sections.
	// All state is owned by one thread and is fixed-size: section labels are static string literals, kept by pointer.
	// Writes and cross-thread reads share the owner's mutex (the one guarding its current task), so a reader sees
	// one coherent execution: its number, phase, ages and name never come from two different tasks.
	class TaskExecutionTracker {
	public:
		enum Phase : uint8 {
			IDLE = 0,
			DISPATCH, // blockMutex taken, Task::doExecute not entered yet
			SETUP, // local transaction start
			RUN,
			COMMIT, // final local transaction commit, including release of its temporary objects
			FINISH, // owner bookkeeping until blockMutex is released
			PHASE_COUNT
		};

		static const int MAX_SECTIONS = 8;
		static const int NAME_SIZE = 96;

		struct Section {
			const char* label;
			uint64 nanos;
			uint32 count;
		};

		struct Execution {
			uint64 id; // per-thread execution number; 0 = none
			uint8 phase;
			uint64 holdStart; // steady-clock nanoseconds
			uint64 phaseStart;
			uint64 phaseNanos[PHASE_COUNT]; // completed time in each phase
			uint64 holdNanos; // set when finished
			const char* section; // open section, or nullptr
			uint64 sectionStart;
			int sectionCount;
			uint32 droppedSections; // section entries whose label did not fit
			Section sections[MAX_SECTIONS];
			char taskName[NAME_SIZE]; // set when finished

			void clear();
		};

	private:
		std::mutex& mutex;
		Execution current;
		Execution completed;
		uint64 executions;
		int depth; // Task::doExecute nesting on the owner thread; only the outermost call moves phases

		static thread_local TaskExecutionTracker* threadTracker;

		void closeSection(uint64 now);
		void closePhase(uint64 now);

	public:
		// mutex must outlive the tracker; the owner thread must not hold it when calling begin/finish/enter/section.
		explicit TaskExecutionTracker(std::mutex& ownerMutex);

		static uint64 now() {
			return (uint64) std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
		}

		// Owner thread: install for the thread's lifetime (nullptr removes it).
		static void install(TaskExecutionTracker* tracker) {
			threadTracker = tracker;
		}

		// Owner thread, with blockMutex held: around each task. The Locked forms are for an owner that publishes its
		// current task under the same mutex, so a reader never sees an execution without its task or the reverse.
		void begin();
		void finish(Task* task);
		void beginLocked(uint64 at);
		void finishLocked(const char* taskName, uint64 at);

		// The last finished execution; read only by the owner thread, which is the only writer.
		const Execution& getCompleted() const {
			return completed;
		}

		// Any thread, with the owner mutex held by the caller.
		void copyCurrentLocked(Execution& out) const {
			out = current;
		}

		// Any thread: the finished execution with this number, if it is still the last one.
		bool copyCompleted(uint64 id, Execution& out) const;

		// Called on whatever thread runs a task; no effect on threads without a tracker.
		static void enter(Phase phase);
		static void section(const char* staticLabel);
		static uint64 currentExecutionId();

		// Nesting guard for Task::doExecute: a task executed synchronously from inside another task's run() must not
		// move the outer execution's phases.
		class Scope {
			bool outermost;
		public:
			Scope();
			~Scope();

			bool isOutermost() const {
				return outermost;
			}
		};

		static const char* phaseName(uint8 phase);

		// "exec #12 RUN 503.2 ms (section 'x' 480.1 ms), held 504.0 ms"
		static void describeCurrent(const Execution& execution, uint64 nowNanos, char* out, int size);
		// "exec #12 held 781.0 ms = dispatch 0.0 + setup 0.0 + run 760.2 + commit 20.7 + finish 0.1 ms; sections x 700.0 ms x1, ..."
		static void describeCompleted(const Execution& execution, char* out, int size);
	};

  } // namespace core
} // namespace engine

using namespace engine::core;

#endif /* ENGINE_CORE_TASKEXECUTIONTRACKER_H_ */
