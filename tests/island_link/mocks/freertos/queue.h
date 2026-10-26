#pragma once

#include "FreeRTOS.h"
#include <string.h>

struct StaticQueue_t {
  uint8_t* storage;
  size_t item_size;
  size_t capacity;
  size_t head;
  size_t count;
};
typedef StaticQueue_t* QueueHandle_t;

inline QueueHandle_t xQueueCreateStatic(UBaseType_t depth, UBaseType_t item_size,
                                        uint8_t* storage, StaticQueue_t* queue) {
  queue->storage = storage;
  queue->item_size = item_size;
  queue->capacity = depth;
  queue->head = queue->count = 0;
  return queue;
}

inline BaseType_t xQueueSend(QueueHandle_t queue, const void* item, unsigned int) {
  if (!queue || queue->count == queue->capacity) return pdFALSE;
  const size_t tail = (queue->head + queue->count) % queue->capacity;
  memcpy(queue->storage + tail * queue->item_size, item, queue->item_size);
  ++queue->count;
  return pdTRUE;
}

inline BaseType_t xQueueReceive(QueueHandle_t queue, void* item, unsigned int) {
  if (!queue || queue->count == 0) return pdFALSE;
  memcpy(item, queue->storage + queue->head * queue->item_size, queue->item_size);
  queue->head = (queue->head + 1) % queue->capacity;
  --queue->count;
  return pdTRUE;
}

inline UBaseType_t uxQueueMessagesWaiting(QueueHandle_t queue) {
  return queue ? queue->count : 0;
}
