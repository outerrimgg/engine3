/*
** Copyright (C) 2007-2019 SWGEmu
** See file COPYING for copying conditions.
*/

#include "TaskExecutionTracker.h"

#include "Task.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

thread_local TaskExecutionTracker* TaskExecutionTracker::threadTracker = nullptr;

namespace {
	thread_local int doExecuteDepth = 0;

	double millis(uint64 nanos) {
		return nanos / 1000000.0;
	}

	// snprintf into the unused tail of out; returns the new length, never past size - 1.
	int append(char* out, int size, int length, const char* format, ...) __attribute__((format(printf, 4, 5)));

	int append(char* out, int size, int length, const char* format, ...) {
		if (length >= size - 1)
			return length;

		va_list args;
		va_start(args, format);
		int written = vsnprintf(out + length, size - length, format, args);
		va_end(args);

		if (written < 0)
			return length;

		return length + written < size - 1 ? length + written : size - 1;
	}
}

void TaskExecutionTracker::Execution::clear() {
	memset(this, 0, sizeof(Execution));
}

TaskExecutionTracker::TaskExecutionTracker(std::mutex& ownerMutex) : mutex(ownerMutex), executions(0), depth(0) {
	current.clear();
	completed.clear();
}

void TaskExecutionTracker::closeSection(uint64 now) {
	if (current.section == nullptr)
		return;

	const uint64 elapsed = now - current.sectionStart;

	for (int i = 0; i < current.sectionCount; ++i) {
		Section& entry = current.sections[i];

		if (entry.label == current.section) {
			entry.nanos += elapsed;
			++entry.count;
			current.section = nullptr;
			return;
		}
	}

	if (current.sectionCount < MAX_SECTIONS) {
		Section& entry = current.sections[current.sectionCount++];
		entry.label = current.section;
		entry.nanos = elapsed;
		entry.count = 1;
	} else {
		++current.droppedSections;
	}

	current.section = nullptr;
}

void TaskExecutionTracker::closePhase(uint64 now) {
	if (current.phase != IDLE && current.phase < PHASE_COUNT)
		current.phaseNanos[current.phase] += now - current.phaseStart;

	current.phaseStart = now;
}

void TaskExecutionTracker::begin() {
	const uint64 at = now();

	std::lock_guard<std::mutex> lk(mutex);

	beginLocked(at);
}

void TaskExecutionTracker::beginLocked(uint64 at) {
	current.clear();
	current.id = ++executions;
	current.phase = DISPATCH;
	current.holdStart = at;
	current.phaseStart = at;
}

void TaskExecutionTracker::finish(Task* task) {
	const char* name = task != nullptr ? task->getTaskName() : nullptr;
	const uint64 at = now();

	std::lock_guard<std::mutex> lk(mutex);

	finishLocked(name, at);
}

void TaskExecutionTracker::finishLocked(const char* name, uint64 at) {
	closeSection(at);
	closePhase(at);

	current.phase = IDLE;
	current.holdNanos = at - current.holdStart;

	if (name != nullptr) {
		strncpy(current.taskName, name, NAME_SIZE - 1);
		current.taskName[NAME_SIZE - 1] = '\0';
	}

	completed = current;
}

bool TaskExecutionTracker::copyCompleted(uint64 id, Execution& out) const {
	std::lock_guard<std::mutex> lk(mutex);

	if (id == 0 || completed.id != id)
		return false;

	out = completed;

	return true;
}

void TaskExecutionTracker::enter(Phase phase) {
	TaskExecutionTracker* tracker = threadTracker;

	if (tracker == nullptr || tracker->depth != 1)
		return;

	const uint64 at = now();

	std::lock_guard<std::mutex> lk(tracker->mutex);

	if (tracker->current.phase == IDLE)
		return;

	if (phase != RUN)
		tracker->closeSection(at);

	tracker->closePhase(at);
	tracker->current.phase = phase;
}

void TaskExecutionTracker::section(const char* staticLabel) {
	TaskExecutionTracker* tracker = threadTracker;

	if (tracker == nullptr || tracker->depth != 1)
		return;

	const uint64 at = now();

	std::lock_guard<std::mutex> lk(tracker->mutex);

	if (tracker->current.phase != RUN)
		return;

	tracker->closeSection(at);

	if (staticLabel != nullptr) {
		tracker->current.section = staticLabel;
		tracker->current.sectionStart = at;
	}
}

uint64 TaskExecutionTracker::currentExecutionId() {
	TaskExecutionTracker* tracker = threadTracker;

	if (tracker == nullptr)
		return 0;

	// Only the owner thread writes current.id.
	return tracker->current.phase != IDLE ? tracker->current.id : 0;
}

TaskExecutionTracker::Scope::Scope() {
	outermost = ++doExecuteDepth == 1;

	if (threadTracker != nullptr)
		threadTracker->depth = doExecuteDepth;
}

TaskExecutionTracker::Scope::~Scope() {
	--doExecuteDepth;

	if (threadTracker != nullptr)
		threadTracker->depth = doExecuteDepth;
}

const char* TaskExecutionTracker::phaseName(uint8 phase) {
	switch (phase) {
	case IDLE:
		return "IDLE";
	case DISPATCH:
		return "DISPATCH";
	case SETUP:
		return "SETUP";
	case RUN:
		return "RUN";
	case COMMIT:
		return "COMMIT";
	case FINISH:
		return "FINISH";
	default:
		return "?";
	}
}

void TaskExecutionTracker::describeCurrent(const Execution& execution, uint64 nowNanos, char* out, int size) {
	if (size <= 0)
		return;

	out[0] = '\0';

	if (execution.id == 0 || execution.phase == IDLE) {
		append(out, size, 0, "idle");
		return;
	}

	int length = append(out, size, 0, "exec #%llu %s %.1f ms", (unsigned long long) execution.id, phaseName(execution.phase),
			millis(nowNanos - execution.phaseStart));

	if (execution.section != nullptr)
		length = append(out, size, length, " (section '%s' %.1f ms)", execution.section, millis(nowNanos - execution.sectionStart));

	append(out, size, length, ", held %.1f ms", millis(nowNanos - execution.holdStart));
}

void TaskExecutionTracker::describeCompleted(const Execution& execution, char* out, int size) {
	if (size <= 0)
		return;

	int length = append(out, size, 0, "exec #%llu held %.1f ms = dispatch %.1f + setup %.1f + run %.1f + commit %.1f + finish %.1f ms",
			(unsigned long long) execution.id, millis(execution.holdNanos), millis(execution.phaseNanos[DISPATCH]),
			millis(execution.phaseNanos[SETUP]), millis(execution.phaseNanos[RUN]), millis(execution.phaseNanos[COMMIT]),
			millis(execution.phaseNanos[FINISH]));

	for (int i = 0; i < execution.sectionCount; ++i) {
		const Section& entry = execution.sections[i];
		length = append(out, size, length, "%s%s %.1f ms x%u", i == 0 ? "; sections " : ", ", entry.label, millis(entry.nanos), entry.count);
	}

	if (execution.droppedSections > 0)
		append(out, size, length, " (+%u untracked)", execution.droppedSections);
}
