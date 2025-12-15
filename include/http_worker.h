#pragma once

#include <stdint.h>

typedef void (*http_worker_job_fn)(void*);

void http_worker_begin();
bool http_worker_enqueue(http_worker_job_fn fn, void* ctx);

uint8_t http_worker_queue_waiting();
uint8_t http_worker_queue_free();
uint8_t http_worker_queue_capacity();

uint32_t http_worker_enqueue_ok_count();
uint32_t http_worker_enqueue_fail_count();
