/*
** Copyright (C) 2007-2019 SWGEmu
** See file COPYING for copying conditions.
*/

#ifndef TASKSCHEDULER_H_
#define TASKSCHEDULER_H_

#include "system/lang.h"

#include <atomic>
#include <mutex>

#include "system/util/SynchronizedHashTable.h"

#include "engine/log/Logger.h"

#include "Task.h"

#include "TimedTaskQueue.h"

namespace engine {
  namespace core {

	class TaskScheduler : public Thread, public Object, public Logger {
		TaskManager* taskManager;

		TimedTaskQueue tasks;

		AtomicBoolean doRun;

		Mutex blockMutex;

		// Outer Rim: what the scheduler is executing right now, for the save-barrier diagnostics in TaskManagerImpl.
		Task* currentTask;
		mutable std::mutex currentTaskMutex;
		TaskExecutionTracker executionTracker;

		HashTable<String, uint64> tasksCount;
		ReadWriteLock tasksCountGuard;
		AtomicBoolean pause;

	protected:
		void prepareTask(Task* task);

	public:
		TaskScheduler();
		TaskScheduler(const String& s);

		virtual ~TaskScheduler();

		void start();

		void run();

		void stop();

		void setPause(bool val);

		inline Task* getCurrentTask() const {
			return currentTask;
		}

		// Outer Rim: the running task's name, copied - safe from another thread (the save-barrier watcher). The owner
		// sets and clears currentTask under currentTaskMutex while it still holds the task, so it cannot be freed here.
		String getCurrentTaskName() const;

		// Outer Rim: the running task's raw name and its execution state, read together under currentTaskMutex.
		void getCurrentExecution(String& taskName, TaskExecutionTracker::Execution& execution) const;

		// Outer Rim: the finished execution with this number, if it is still this thread's last one.
		bool getCompletedExecution(uint64 executionId, TaskExecutionTracker::Execution& execution) const {
			return executionTracker.copyCompleted(executionId, execution);
		}

		bool scheduleTask(Task* task, uint64 delay = 0);
		bool scheduleTask(Task* task, const Time& time);

		bool cancelTask(Task* task);

		void addSchedulerTasks(TaskScheduler* scheduler);

		HashTable<String, uint64> getTasksCount();

		void flushTasks() {
			tasks.flush();
		}

		void clearTasks() {
			tasks.clear();
		}

		void fixQueue() {
			tasks.repair();
		}

		inline int getQueueSize() const {
			return tasks.size();
		}

		inline uint64 getPushedTasks() const {
			return tasks.getPushedEntries();
		}

		inline uint64 getPoppedTasks() const {
			return tasks.getPoppedEntries();
		}

		inline uint64 getRemovedTasks() const {
			return tasks.getRemovedEntries();
		}

		inline void printTasks() {
			tasks.printQueue();
		}

		inline TaskManager* getTaskManager() {
			return taskManager;
		}

		inline void setTaskManager(TaskManager* manager) {
			taskManager = manager;
		}

		inline Mutex* getBlockMutex() {
			return &blockMutex;
		}
	};

  } // namespace core
} // namespace engine

using namespace engine::core;

#endif /* TASKSCHEDULER_H_ */
