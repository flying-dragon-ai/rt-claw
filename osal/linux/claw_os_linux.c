/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * OSAL implementation for Linux (POSIX pthreads).
 */

#include "osal/claw_os.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <sched.h>

/* ---------- helpers ---------- */

static void ms_to_abstime(uint32_t ms, struct timespec *ts)
{
    clock_gettime(CLOCK_REALTIME, ts);
    ts->tv_sec  += ms / 1000;
    ts->tv_nsec += (long)(ms % 1000) * 1000000L;
    if (ts->tv_nsec >= 1000000000L) {
        ts->tv_sec  += 1;
        ts->tv_nsec -= 1000000000L;
    }
}

/* ---------- Thread ---------- */

typedef struct {
    pthread_t       handle;
    void          (*entry)(void *arg);
    void           *arg;
    atomic_bool     exit_flag;
} linux_thread_t;

static _Thread_local linux_thread_t *s_tls_self;

static void *thread_wrapper(void *param)
{
    linux_thread_t *t = (linux_thread_t *)param;
    s_tls_self = t;
    t->entry(t->arg);
    return NULL;
}

int claw_thread_should_exit(void)
{
    linux_thread_t *t = s_tls_self;
    if (t) {
        return atomic_load(&t->exit_flag) ? 1 : 0;
    }
    return 0;
}

claw_thread_t claw_thread_create(const char *name,
                                  void (*entry)(void *arg),
                                  void *arg,
                                  uint32_t stack_size,
                                  uint32_t priority)
{
    (void)name;
    (void)stack_size;
    (void)priority;

    linux_thread_t *t = malloc(sizeof(*t));
    if (!t) {
        return NULL;
    }

    t->entry = entry;
    t->arg = arg;
    atomic_init(&t->exit_flag, false);

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

    int ret = pthread_create(&t->handle, &attr, thread_wrapper, t);
    pthread_attr_destroy(&attr);

    if (ret != 0) {
        free(t);
        return NULL;
    }

    return (claw_thread_t)t;
}

void claw_thread_delete(claw_thread_t thread)
{
    if (!thread) {
        return;
    }

    linux_thread_t *t = (linux_thread_t *)thread;
    atomic_store(&t->exit_flag, true);
    pthread_join(t->handle, NULL);
    free(t);
}

void claw_thread_delay_ms(uint32_t ms)
{
    /*
     * Sleep in 200ms chunks so cooperative exit via
     * claw_thread_should_exit() is responsive.
     */
    uint32_t remaining = ms;

    while (remaining > 0 && !claw_thread_should_exit()) {
        uint32_t chunk = (remaining > 200) ? 200 : remaining;
        struct timespec ts;
        ts.tv_sec  = chunk / 1000;
        ts.tv_nsec = (long)(chunk % 1000) * 1000000L;
        nanosleep(&ts, NULL);
        remaining -= chunk;
    }
}

void claw_thread_yield(void)
{
    sched_yield();
}

/* ---------- Mutex ---------- */

claw_mutex_t claw_mutex_create(const char *name)
{
    (void)name;
    pthread_mutex_t *m = malloc(sizeof(*m));
    if (!m) {
        return NULL;
    }
    pthread_mutex_init(m, NULL);
    return (claw_mutex_t)m;
}

int claw_mutex_lock(claw_mutex_t mutex, uint32_t timeout_ms)
{
    if (!mutex) {
        return CLAW_ERROR;
    }
    pthread_mutex_t *m = (pthread_mutex_t *)mutex;

    if (timeout_ms == CLAW_WAIT_FOREVER) {
        return (pthread_mutex_lock(m) == 0) ? CLAW_OK : CLAW_ERROR;
    }
    if (timeout_ms == CLAW_NO_WAIT) {
        return (pthread_mutex_trylock(m) == 0) ? CLAW_OK : CLAW_TIMEOUT;
    }

    struct timespec ts;
    ms_to_abstime(timeout_ms, &ts);
    int ret = pthread_mutex_timedlock(m, &ts);
    if (ret == 0) {
        return CLAW_OK;
    }
    if (ret == ETIMEDOUT) {
        return CLAW_TIMEOUT;
    }
    return CLAW_ERROR;
}

void claw_mutex_unlock(claw_mutex_t mutex)
{
    if (mutex) {
        pthread_mutex_unlock((pthread_mutex_t *)mutex);
    }
}

void claw_mutex_delete(claw_mutex_t mutex)
{
    if (mutex) {
        pthread_mutex_destroy((pthread_mutex_t *)mutex);
        free(mutex);
    }
}

/* ---------- Semaphore ---------- */

claw_sem_t claw_sem_create(const char *name, uint32_t init_value)
{
    (void)name;
    sem_t *s = malloc(sizeof(*s));
    if (!s) {
        return NULL;
    }
    sem_init(s, 0, init_value);
    return (claw_sem_t)s;
}

int claw_sem_take(claw_sem_t sem, uint32_t timeout_ms)
{
    if (!sem) {
        return CLAW_ERROR;
    }
    sem_t *s = (sem_t *)sem;

    if (timeout_ms == CLAW_WAIT_FOREVER) {
        while (sem_wait(s) != 0) {
            if (errno != EINTR) {
                return CLAW_ERROR;
            }
        }
        return CLAW_OK;
    }
    if (timeout_ms == CLAW_NO_WAIT) {
        return (sem_trywait(s) == 0) ? CLAW_OK : CLAW_TIMEOUT;
    }

    struct timespec ts;
    ms_to_abstime(timeout_ms, &ts);
    while (sem_timedwait(s, &ts) != 0) {
        if (errno == ETIMEDOUT) {
            return CLAW_TIMEOUT;
        }
        if (errno != EINTR) {
            return CLAW_ERROR;
        }
    }
    return CLAW_OK;
}

void claw_sem_give(claw_sem_t sem)
{
    if (sem) {
        sem_post((sem_t *)sem);
    }
}

void claw_sem_delete(claw_sem_t sem)
{
    if (sem) {
        sem_destroy((sem_t *)sem);
        free(sem);
    }
}

/* ---------- Message Queue ---------- */

typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t  not_empty;
    pthread_cond_t  not_full;
    uint8_t        *buf;
    uint32_t        msg_size;
    uint32_t        capacity;
    uint32_t        count;
    uint32_t        head;
    uint32_t        tail;
} linux_mq_t;

claw_mq_t claw_mq_create(const char *name,
                           uint32_t msg_size,
                           uint32_t max_msgs)
{
    (void)name;
    linux_mq_t *mq = malloc(sizeof(*mq));
    if (!mq) {
        return NULL;
    }

    mq->buf = malloc(msg_size * max_msgs);
    if (!mq->buf) {
        free(mq);
        return NULL;
    }

    pthread_mutex_init(&mq->lock, NULL);
    pthread_cond_init(&mq->not_empty, NULL);
    pthread_cond_init(&mq->not_full, NULL);
    mq->msg_size = msg_size;
    mq->capacity = max_msgs;
    mq->count = 0;
    mq->head = 0;
    mq->tail = 0;

    return (claw_mq_t)mq;
}

int claw_mq_send(claw_mq_t mq_handle, const void *msg, uint32_t size,
                  uint32_t timeout_ms)
{
    if (!mq_handle) {
        return CLAW_ERROR;
    }
    (void)size;
    linux_mq_t *mq = (linux_mq_t *)mq_handle;

    pthread_mutex_lock(&mq->lock);

    if (mq->count >= mq->capacity) {
        if (timeout_ms == CLAW_NO_WAIT) {
            pthread_mutex_unlock(&mq->lock);
            return CLAW_TIMEOUT;
        }
        if (timeout_ms == CLAW_WAIT_FOREVER) {
            while (mq->count >= mq->capacity) {
                pthread_cond_wait(&mq->not_full, &mq->lock);
            }
        } else {
            struct timespec ts;
            ms_to_abstime(timeout_ms, &ts);
            while (mq->count >= mq->capacity) {
                int ret = pthread_cond_timedwait(&mq->not_full,
                                                  &mq->lock, &ts);
                if (ret == ETIMEDOUT) {
                    pthread_mutex_unlock(&mq->lock);
                    return CLAW_TIMEOUT;
                }
            }
        }
    }

    memcpy(mq->buf + mq->tail * mq->msg_size, msg, mq->msg_size);
    mq->tail = (mq->tail + 1) % mq->capacity;
    mq->count++;

    pthread_cond_signal(&mq->not_empty);
    pthread_mutex_unlock(&mq->lock);
    return CLAW_OK;
}

int claw_mq_recv(claw_mq_t mq_handle, void *msg, uint32_t size,
                  uint32_t timeout_ms)
{
    if (!mq_handle) {
        return CLAW_ERROR;
    }
    (void)size;
    linux_mq_t *mq = (linux_mq_t *)mq_handle;

    pthread_mutex_lock(&mq->lock);

    if (mq->count == 0) {
        if (timeout_ms == CLAW_NO_WAIT) {
            pthread_mutex_unlock(&mq->lock);
            return CLAW_TIMEOUT;
        }
        if (timeout_ms == CLAW_WAIT_FOREVER) {
            while (mq->count == 0) {
                pthread_cond_wait(&mq->not_empty, &mq->lock);
            }
        } else {
            struct timespec ts;
            ms_to_abstime(timeout_ms, &ts);
            while (mq->count == 0) {
                int ret = pthread_cond_timedwait(&mq->not_empty,
                                                  &mq->lock, &ts);
                if (ret == ETIMEDOUT) {
                    pthread_mutex_unlock(&mq->lock);
                    return CLAW_TIMEOUT;
                }
            }
        }
    }

    memcpy(msg, mq->buf + mq->head * mq->msg_size, mq->msg_size);
    mq->head = (mq->head + 1) % mq->capacity;
    mq->count--;

    pthread_cond_signal(&mq->not_full);
    pthread_mutex_unlock(&mq->lock);
    return CLAW_OK;
}

void claw_mq_delete(claw_mq_t mq_handle)
{
    if (!mq_handle) {
        return;
    }
    linux_mq_t *mq = (linux_mq_t *)mq_handle;
    pthread_mutex_destroy(&mq->lock);
    pthread_cond_destroy(&mq->not_empty);
    pthread_cond_destroy(&mq->not_full);
    free(mq->buf);
    free(mq);
}

/* ---------- Timer ---------- */

typedef struct linux_timer linux_timer_t;
struct linux_timer {
    void          (*callback)(void *arg);
    void           *arg;
    uint32_t        period_ms;
    int             repeat;
    atomic_bool     running;
    atomic_bool     deleted;
    pthread_t       thread;
    pthread_mutex_t lock;
    pthread_cond_t  cond;
};

static void *timer_thread_fn(void *param)
{
    linux_timer_t *t = (linux_timer_t *)param;

    pthread_mutex_lock(&t->lock);
    while (!atomic_load(&t->deleted)) {
        /* Wait until running */
        while (!atomic_load(&t->running) && !atomic_load(&t->deleted)) {
            pthread_cond_wait(&t->cond, &t->lock);
        }
        if (atomic_load(&t->deleted)) {
            break;
        }

        /* Sleep for period */
        struct timespec ts;
        ms_to_abstime(t->period_ms, &ts);
        int ret = pthread_cond_timedwait(&t->cond, &t->lock, &ts);

        if (atomic_load(&t->deleted)) {
            break;
        }
        if (!atomic_load(&t->running)) {
            continue;
        }
        if (ret == ETIMEDOUT) {
            /* Fire callback */
            pthread_mutex_unlock(&t->lock);
            t->callback(t->arg);
            pthread_mutex_lock(&t->lock);

            if (!t->repeat) {
                atomic_store(&t->running, false);
            }
        }
    }
    pthread_mutex_unlock(&t->lock);
    return NULL;
}

claw_timer_t claw_timer_create(const char *name,
                                void (*callback)(void *arg),
                                void *arg,
                                uint32_t period_ms,
                                int repeat)
{
    (void)name;
    linux_timer_t *t = malloc(sizeof(*t));
    if (!t) {
        return NULL;
    }

    t->callback  = callback;
    t->arg       = arg;
    t->period_ms = period_ms;
    t->repeat    = repeat;
    atomic_init(&t->running, false);
    atomic_init(&t->deleted, false);
    pthread_mutex_init(&t->lock, NULL);
    pthread_cond_init(&t->cond, NULL);

    if (pthread_create(&t->thread, NULL, timer_thread_fn, t) != 0) {
        pthread_mutex_destroy(&t->lock);
        pthread_cond_destroy(&t->cond);
        free(t);
        return NULL;
    }

    return (claw_timer_t)t;
}

void claw_timer_start(claw_timer_t timer)
{
    if (!timer) {
        return;
    }
    linux_timer_t *t = (linux_timer_t *)timer;
    pthread_mutex_lock(&t->lock);
    atomic_store(&t->running, true);
    pthread_cond_signal(&t->cond);
    pthread_mutex_unlock(&t->lock);
}

void claw_timer_stop(claw_timer_t timer)
{
    if (!timer) {
        return;
    }
    linux_timer_t *t = (linux_timer_t *)timer;
    pthread_mutex_lock(&t->lock);
    atomic_store(&t->running, false);
    pthread_cond_signal(&t->cond);
    pthread_mutex_unlock(&t->lock);
}

void claw_timer_delete(claw_timer_t timer)
{
    if (!timer) {
        return;
    }
    linux_timer_t *t = (linux_timer_t *)timer;
    pthread_mutex_lock(&t->lock);
    atomic_store(&t->deleted, true);
    pthread_cond_signal(&t->cond);
    pthread_mutex_unlock(&t->lock);
    pthread_join(t->thread, NULL);
    pthread_mutex_destroy(&t->lock);
    pthread_cond_destroy(&t->cond);
    free(t);
}

/* ---------- Memory ---------- */

void *claw_malloc(size_t size)
{
    return malloc(size);
}

void *claw_calloc(size_t nmemb, size_t size)
{
    return calloc(nmemb, size);
}

void claw_free(void *ptr)
{
    free(ptr);
}

/* ---------- Log ---------- */

static int s_log_enabled = 1;
static int s_log_level = CLAW_LOG_DEBUG;

void claw_log_set_enabled(int enabled)
{
    s_log_enabled = enabled;
}

int claw_log_get_enabled(void)
{
    return s_log_enabled;
}

void claw_log_set_level(int level)
{
    if (level < CLAW_LOG_ERROR) {
        level = CLAW_LOG_ERROR;
    }
    if (level > CLAW_LOG_DEBUG) {
        level = CLAW_LOG_DEBUG;
    }
    s_log_level = level;
}

int claw_log_get_level(void)
{
    return s_log_level;
}

static const char *s_level_letters[] = { "E", "W", "I", "D" };
static const char *s_level_colors[]  = {
    "\033[0;31m",   /* E: red */
    "\033[0;33m",   /* W: yellow */
    "\033[0;32m",   /* I: green */
    "\033[0;36m",   /* D: cyan */
};

void claw_log(int level, const char *tag, const char *fmt, ...)
{
    va_list ap;

    if (!s_log_enabled || level > s_log_level) {
        return;
    }
    if (level < 0) {
        level = 0;
    }
    if (level > 3) {
        level = 3;
    }

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    unsigned long ms = (unsigned long)ts.tv_sec * 1000
                     + (unsigned long)ts.tv_nsec / 1000000;

    fprintf(stderr, "%s%s (%lu) %s: ",
            s_level_colors[level], s_level_letters[level], ms, tag);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\033[0m\n");
}

void claw_log_raw(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
}

/* ---------- Time ---------- */

static struct timespec s_boot_time;
static pthread_once_t s_boot_once = PTHREAD_ONCE_INIT;

static void init_boot_time(void)
{
    clock_gettime(CLOCK_MONOTONIC, &s_boot_time);
}

uint32_t claw_tick_get(void)
{
    return claw_tick_ms();
}

uint32_t claw_tick_ms(void)
{
    pthread_once(&s_boot_once, init_boot_time);

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    uint32_t ms = (uint32_t)((now.tv_sec - s_boot_time.tv_sec) * 1000
                  + (now.tv_nsec - s_boot_time.tv_nsec) / 1000000);
    return ms;
}
