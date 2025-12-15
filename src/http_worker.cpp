#include "http_worker.h"

#include <Arduino.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

struct HttpWorkerJob {
    http_worker_job_fn fn;
    void* ctx;
};

static QueueHandle_t g_queue = nullptr;
static TaskHandle_t g_task = nullptr;
static uint8_t g_queueCapacity = 0;
static uint32_t g_enqueueOk = 0;
static uint32_t g_enqueueFail = 0;

static void http_worker_task(void* arg)
{
    (void)arg;

    for (;;) {
        HttpWorkerJob job;
        if (xQueueReceive(g_queue, &job, portMAX_DELAY) == pdTRUE) {
            if (job.fn) {
                job.fn(job.ctx);
            }
        }
    }
}

void http_worker_begin()
{
    if (g_queue) return;

    g_queueCapacity = 8;
    g_queue = xQueueCreate(g_queueCapacity, sizeof(HttpWorkerJob));
    if (!g_queue) {
        Serial.println("HTTP worker: failed to create queue");
        return;
    }

    BaseType_t ok = xTaskCreatePinnedToCore(
        http_worker_task,
        "http_worker",
        8192,
        nullptr,
        1,
        &g_task,
        0
    );

    if (ok != pdPASS) {
        Serial.println("HTTP worker: failed to create task");
        vQueueDelete(g_queue);
        g_queue = nullptr;
        g_task = nullptr;
    }
}

bool http_worker_enqueue(http_worker_job_fn fn, void* ctx)
{
    if (!g_queue || !fn) return false;

    HttpWorkerJob job{fn, ctx};
    if (xQueueSend(g_queue, &job, 0) == pdTRUE) {
        g_enqueueOk++;
        return true;
    }
    g_enqueueFail++;
    return false;
}

uint8_t http_worker_queue_waiting()
{
    if (!g_queue) return 0;
    return (uint8_t)uxQueueMessagesWaiting(g_queue);
}

uint8_t http_worker_queue_free()
{
    if (!g_queue) return 0;
    return (uint8_t)uxQueueSpacesAvailable(g_queue);
}

uint8_t http_worker_queue_capacity()
{
    return g_queueCapacity;
}

uint32_t http_worker_enqueue_ok_count()
{
    return g_enqueueOk;
}

uint32_t http_worker_enqueue_fail_count()
{
    return g_enqueueFail;
}
