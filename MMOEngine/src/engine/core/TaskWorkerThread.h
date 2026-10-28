/*
** Copyright (C) 2007-2019 SWGEmu
** See file COPYING for copying conditions.
*/

#ifndef TASKWORKERTHREAD_H_
#define TASKWORKERTHREAD_H_

#include "engine/service/ServiceThread.h"

#include "RunStatistics.h"
#include "TaskExecutionTracker.h"

#include <mutex>

namespace engine {
  namespace core {

	class TaskManager;
	class TaskQueue;
	class Task;
	class ManagedObject;

	class TaskWorkerThread : public ServiceThread {
		Mutex blockMutex;
		TaskQueue* queue;
		int cpu;

		bool blockDuringSave;

		AtomicBoolean pauseWorker;
		AtomicBoolean initializeDBHandles;

		Task* currentTask;
		mutable std::mutex currentTaskMutex;
		TaskExecutionTracker executionTracker;

		uint64 mutexWaitTime;
		uint64 mutexesAcquired;
		uint64 totalBdbTime;
		uint64 totalBdbQueries;

#ifdef COLLECT_TASKSTATISTICS
		HashTable<String, RunStatistics> tasksStatistics;
		VectorMap<String, RunStatistics> luaTasksStatistics;
		VectorMap<String, RunStatistics> bdbReadStatistics;
		VectorMap<String, RunStatistics> mysqlStatistics;

		ReadWriteLock tasksStatsGuard;

		uint64 totalTaskRunCount;
		uint64 totalBdbReadCount;

		int samplingRate;
		int bdbSamplingRate;
#endif

	public:
		TaskWorkerThread(const String& s, TaskQueue* queue, int cpu, bool blockDuringSave);

		virtual ~TaskWorkerThread();

		void start();

		//void pushToRetryQueue(Task* task);

		virtual void run();

		void stop();

		void signalDBHandleInitialize();

#ifdef COLLECT_TASKSTATISTICS
		HashTable<String, RunStatistics> getTasksStatistics();
		VectorMap<String, RunStatistics> getLuaTasksStatistics();
		VectorMap<String, RunStatistics> getBDBReadStatistics();
		VectorMap<String, RunStatistics> getMysqlStatistics();

		void addLuaTaskStats(const String& name, uint64 runTime);
		void addMysqlStats(const String& query, uint64 runTime);

		void clearTaskStatistics();
		void setStatsDSamplingRate(int val);
		void setStatsDBdbSamplingRate(int val);

#ifdef CXX11_COMPILER
		void addLuaTaskStats(String&& name, uint64 runTime);
#endif
#endif
		void addBDBReadStats(const String& dbName, uint64 runTime);

		void setPause(bool val);

		void logTask(const char* taskName, uint64 elapsedTime) const;

		inline void addMutexWaitTime(uint64 time) {
			mutexesAcquired++;
			mutexWaitTime += time;
		}

		void clearMutexWaitTime() {
			mutexWaitTime = 0;
		}

		uint64 getTotalBdbTime() const {
			return totalBdbTime;
		}

		uint64 getMutexWaitTime() const {
			return mutexWaitTime;
		}

		TaskQueue* getTaskQueue() const {
			return queue;
		}

		TaskWorkerThread* asTaskWorkerThread() {
			return this;
		}

		inline Mutex* getBlockMutex() {
			return &blockMutex;
		}

		inline bool doBlockWorkerDuringSave() const {
			return blockDuringSave;
		}

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

	};

  } // namespace service
} // namespace engine

using namespace engine::core;

#endif /* TASKWORKERTHREAD_H_ */
