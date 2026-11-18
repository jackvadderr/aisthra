/* SPDX-License-Identifier: MIT */

#ifndef AISTHRA_PLATFORM_H
#define AISTHRA_PLATFORM_H

#if defined(AISTHRA_ESP_IDF) || defined(ARDUINO_ARCH_ESP32)

#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <pthread.h>

/*
 * Arduino-ESP32 exposes the ESP-IDF POSIX synchronization subset and the
 * FreeRTOS task identity through its board core.  Keep this adapter private
 * so the public C17 ABI remains independent of Arduino headers.
 */
typedef pthread_mutex_t mtx_t;
typedef pthread_cond_t cnd_t;
typedef TaskHandle_t aisthra_thread_id_t;

enum {
    mtx_plain = 0,
    thrd_success = 0
};

static inline int mtx_init(mtx_t *mutex, int type)
{
    (void)type;
    return pthread_mutex_init(mutex, NULL);
}

static inline int mtx_destroy(mtx_t *mutex)
{
    return pthread_mutex_destroy(mutex);
}

static inline int mtx_lock(mtx_t *mutex)
{
    return pthread_mutex_lock(mutex);
}

static inline int mtx_unlock(mtx_t *mutex)
{
    return pthread_mutex_unlock(mutex);
}

static inline int cnd_init(cnd_t *condition)
{
    return pthread_cond_init(condition, NULL);
}

static inline int cnd_destroy(cnd_t *condition)
{
    return pthread_cond_destroy(condition);
}

static inline int cnd_wait(cnd_t *condition, mtx_t *mutex)
{
    return pthread_cond_wait(condition, mutex);
}

static inline int cnd_signal(cnd_t *condition)
{
    return pthread_cond_signal(condition);
}

static inline int cnd_broadcast(cnd_t *condition)
{
    return pthread_cond_broadcast(condition);
}

static inline aisthra_thread_id_t aisthra_thread_current(void)
{
    return xTaskGetCurrentTaskHandle();
}

static inline int aisthra_thread_equal(
    aisthra_thread_id_t left, aisthra_thread_id_t right)
{
    return left == right;
}

static inline double aisthra_platform_monotonic_seconds(void)
{
    int64_t microseconds = esp_timer_get_time();
    if (microseconds < 0) {
        return -1.0;
    }
    return (double)microseconds / 1000000.0;
}

#elif defined(AISTHRA_ZEPHYR)

#include <zephyr/kernel.h>

/* Zephyr's kernel primitives stay private to the platform adapter. */
typedef struct aisthra_zephyr_mutex {
    struct k_mutex native;
} mtx_t;

typedef struct aisthra_zephyr_condvar {
    struct k_condvar native;
} cnd_t;
typedef k_tid_t aisthra_thread_id_t;

enum {
    mtx_plain = 0,
    thrd_success = 0
};

static inline int mtx_init(mtx_t *mutex, int type)
{
    (void)type;
    return k_mutex_init(&mutex->native) == 0 ? thrd_success : -1;
}

static inline int mtx_destroy(mtx_t *mutex)
{
    (void)mutex;
    return thrd_success;
}

static inline int mtx_lock(mtx_t *mutex)
{
    return k_mutex_lock(&mutex->native, K_FOREVER) == 0 ?
        thrd_success : -1;
}

static inline int mtx_unlock(mtx_t *mutex)
{
    return k_mutex_unlock(&mutex->native) == 0 ? thrd_success : -1;
}

static inline int cnd_init(cnd_t *condition)
{
    return k_condvar_init(&condition->native) == 0 ? thrd_success : -1;
}

static inline int cnd_destroy(cnd_t *condition)
{
    (void)condition;
    return thrd_success;
}

static inline int cnd_wait(cnd_t *condition, mtx_t *mutex)
{
    return k_condvar_wait(
               &condition->native, &mutex->native, K_FOREVER) == 0 ?
        thrd_success : -1;
}

static inline int cnd_signal(cnd_t *condition)
{
    return k_condvar_signal(&condition->native) == 0 ? thrd_success : -1;
}

static inline int cnd_broadcast(cnd_t *condition)
{
    return k_condvar_broadcast(&condition->native) < 0 ? -1 : thrd_success;
}

static inline aisthra_thread_id_t aisthra_thread_current(void)
{
    return k_current_get();
}

static inline int aisthra_thread_equal(
    aisthra_thread_id_t left, aisthra_thread_id_t right)
{
    return left == right;
}

static inline double aisthra_platform_monotonic_seconds(void)
{
    int64_t milliseconds = k_uptime_get();
    if (milliseconds < 0) {
        return -1.0;
    }
    return (double)milliseconds / 1000.0;
}

#else

#include <threads.h>
#include <time.h>

typedef thrd_t aisthra_thread_id_t;

static inline aisthra_thread_id_t aisthra_thread_current(void)
{
    return thrd_current();
}

static inline int aisthra_thread_equal(
    aisthra_thread_id_t left, aisthra_thread_id_t right)
{
    return thrd_equal(left, right) != 0;
}

static inline double aisthra_platform_monotonic_seconds(void)
{
    struct timespec now;
#if defined(CLOCK_MONOTONIC)
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return -1.0;
    }
#else
    return -1.0;
#endif
    return (double)now.tv_sec + (double)now.tv_nsec / 1000000000.0;
}

#endif

#endif
