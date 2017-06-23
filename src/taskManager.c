// MusA  Copyright (C) 2017  Lukáš Ondráček <ondracek.lukas@gmail.com>, see README file

#include "taskManager.h"
#include "util.h"
#include <stdio.h>
#include <stdbool.h>
#include <pthread.h>
#include <unistd.h>

#ifdef TM_LOG
#define LOG printf
#else
#define LOG(...)
#endif

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
bool stopRequested = false;

unsigned workersCnt;
static struct workerInfo {
	unsigned id;
	pthread_t threadID;
	int counter;
	bool sleeping;
#ifdef TM_PROFILER
	struct {
		double time[2];
		int cnt[2];
	} profTasks[1024];
	double profOverhead;
	double profSleep;
	double profWork;
#endif
} *workers;

#ifdef TM_PROFILER

#define profGetTS(ts) \
	clock_gettime(CLOCK_MONOTONIC_RAW, &ts)
#define profAddDiff(val,tsNew,tsOld) { \
	time_t sec = tsNew.tv_sec -tsOld.tv_sec; \
	long  nsec = tsNew.tv_nsec-tsOld.tv_nsec; \
	val += (double)sec + (double)nsec/1000000000.0; \
}

#define PROF_WORKER_BEGIN \
	struct timespec profTS; \
	struct timespec profTSWorker; \
	struct timespec profTSTask; \
	info->profOverhead = 0; info->profSleep = 0; info->profWork=0; \
	for (int i=0; i<1024; i++) { \
		for (int j=0; j<2; j++) { \
			info->profTasks[i].time[j] = 0; \
			info->profTasks[i].cnt[j]  = 0; \
		} \
	} \
	profGetTS(profTSWorker)

#define PROF_SLEEP_BEGIN \
	profGetTS(profTS); \
	profAddDiff(info->profOverhead, profTS, profTSWorker); \
	profTSWorker = profTS

#define PROF_SLEEP_END \
	profGetTS(profTS); \
	profAddDiff(info->profSleep, profTS, profTSWorker); \
	profTSWorker = profTS
	
#define PROF_TASK_BEGIN \
	profGetTS(profTSTask)

#define PROF_TASK_END(priority, result) \
	profGetTS(profTS); \
	profAddDiff(info->profTasks[priority+512].time[result], profTS, profTSTask); \
	info->profTasks[priority+512].cnt[result]++; \
	if (result) { \
		profAddDiff(info->profOverhead, profTSTask, profTSWorker); \
		profAddDiff(info->profWork,     profTS,     profTSTask); \
		profTSWorker = profTS; \
	}

#define PROF_WORKER_END \
	profGetTS(profTS); \
	profAddDiff(info->profOverhead, profTS, profTSWorker); \

#else
#define PROF_WORKER_BEGIN
#define PROF_SLEEP_BEGIN
#define PROF_SLEEP_END
#define PROF_TASK_BEGIN
#define PROF_TASK_END(priority,result)
#define PROF_WORKER_END
#endif


static void *worker(struct workerInfo *info);
static __attribute__((constructor)) void init() {
	workersCnt=sysconf(_SC_NPROCESSORS_ONLN);
	workers=utilMalloc(sizeof(struct workerInfo)*workersCnt);
	for (size_t i=0; i<workersCnt; i++) {
		workers[i].id=i;
		workers[i].counter=0;
		workers[i].sleeping=false;
		pthread_create(&workers[i].threadID, NULL, (void*(*)(void*))worker, workers+i);
	}
}

void tmResume() {
	pthread_cond_broadcast(&cond);
}

__attribute__((destructor)) void tmStop() {
	if (stopRequested) return;
	stopRequested = true;
	pthread_mutex_lock(&mutex);
	pthread_cond_broadcast(&cond);
	pthread_mutex_unlock(&mutex);
	for (int i = 0; i<workersCnt; i++) {
		pthread_join(workers[i].threadID, NULL);
	}
#ifdef TM_PROFILER
	printf("\n");
	printf("                            work |                 overhead |      sleep\n");
	printf("\n");

	double sleepTime = 0, workTime = 0, overheadTime=0;
	for (int w=0; w<workersCnt; w++) {
		sleepTime    += workers[w].profSleep;
		workTime     += workers[w].profWork;
		overheadTime += workers[w].profOverhead;
	}
	double wholeTime = sleepTime + workTime + overheadTime;

	for (int i=0; i<1024; i++) {
		int priority = i-512;
		double time[2] = {0,0};
		int     cnt[2] = {0,0};
		for (int w=0; w<workersCnt; w++) {
			for (int j=0; j<2; j++) {
				time[j] += workers[w].profTasks[i].time[j];
				cnt [j] += workers[w].profTasks[i].cnt[j];
			}
		}
		if ((cnt[0]==0) && (cnt[1]==0)) continue;
		printf("%5d :",
			priority);
		for (int j=1; j>=0; j--) {
			if (cnt[j]>0) {
				printf("    %9.3f ms  %5.2f %% |",
					time[j]/cnt[j]*1000.0,
					time[j]/wholeTime*100.0);
			} else {
				printf("   %9s      %5.2f %% |",
					"",
					0.0);
			}
		}
		printf("\n");
	}
	printf("\n");
	printf("  ALL :                  %5.2f %% |                  %5.2f %% |    %5.2f %%\n",
		workTime/wholeTime*100.0,
		overheadTime/wholeTime*100.0,
		sleepTime/wholeTime*100.0);

	printf("\n");

#endif
}


struct taskHandler {
	struct taskInfo *task;
	bool (*func)();
	int priority;
	struct taskHandler *next;
} *tasks = NULL;

void tmRegister(struct taskInfo *task, bool(*func)(), int priority) {
	task -> running = 0;
	struct taskHandler *handler = malloc(sizeof(struct taskHandler));
	handler -> task = task;
	handler -> func = func;
	handler -> priority = priority;
	struct taskHandler **ph = &tasks;
	while (*ph && ((*ph)->priority < priority)) ph = &(*ph)->next;
	handler -> next = *ph;
	*ph = handler;
}

int *tmBarrierCreate() {
	return malloc(sizeof(int) * workersCnt);
}
void tmBarrierPlace(int *barrier) {
	__sync_synchronize();
	for (int i = 0; i < workersCnt; i++) {
		barrier[i] = workers[i].counter;
	}
}
bool tmBarrierReached(int *barrier) {
	for (int i = 0; i < workersCnt; i++) {
		if ((workers[i].counter - barrier[i] <= 0) && (!workers[i].sleeping)) {
			return false;
		}
	}
	__sync_synchronize();
	return true;
}
void tmBarrierFree(int *barrier) {
	free(barrier);
}


static void *worker(struct workerInfo *info) {
	LOG("Worker %d started...\n", info->id);
	PROF_WORKER_BEGIN;
	
	bool allDone = true;
	while (true) {

		info->counter++;

		if (allDone || stopRequested) {
			pthread_mutex_lock(&mutex);
			if (stopRequested) {
				pthread_mutex_unlock(&mutex);
				break;
			}
			LOG("Worker %d sleeping...\n", info->id);
			info->sleeping = true;
			PROF_SLEEP_BEGIN;
			pthread_cond_wait(&cond, &mutex);
			PROF_SLEEP_END;
			info->sleeping = false;
			LOG("Worker %d awaking...\n", info->id);
			pthread_mutex_unlock(&mutex);
			if (stopRequested) {
				break;
			}
			allDone = false;
			info->counter++;
		}

		struct taskHandler *t = tasks;
		for (; t; t = t->next) {
			if (t->task->active) {
				if (t->task->serial) {
					if (!__sync_bool_compare_and_swap(&t->task->running, 0, 1)) continue;
				} else {
					__sync_fetch_and_add(&t->task->running, 1);
				}
				PROF_TASK_BEGIN;
				bool r = t->func();
				PROF_TASK_END(t->priority, r);
				__sync_fetch_and_sub(&t->task->running, 1);
				if (r) break;
			}
		}
		
		if (t) {
			tmResume(); // ??
		} else {
			LOG("Worker %d: all work done...\n", info->id);
			allDone = true;
		}
	}
	PROF_WORKER_END;
	LOG("Worker %d exitting...\n", info->id);
}