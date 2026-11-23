#pragma once
#include "FreeRTOS.h"
#include <deque>
#include <vector>
#include <memory>
#include <mutex>
#include <cstring>
struct NativeQueue { size_t capacity, itemSize; std::deque<std::vector<unsigned char>> items; std::mutex mutex; };
using QueueHandle_t = NativeQueue *;
namespace NativeQueues {
inline bool &fail() { static bool f = false; return f; }
inline std::vector<std::unique_ptr<NativeQueue>> &storage() { static std::vector<std::unique_ptr<NativeQueue>> v; return v; }
}
inline QueueHandle_t xQueueCreate(unsigned count, unsigned size) {
  if (NativeQueues::fail()) return nullptr;
  std::unique_ptr<NativeQueue> q(new NativeQueue); q->capacity = count; q->itemSize = size;
  auto *result = q.get(); NativeQueues::storage().push_back(std::move(q)); return result;
}
inline BaseType_t xQueueSend(QueueHandle_t q, const void *p, TickType_t wait) {
  if (!q || wait) return pdFALSE;
  std::lock_guard<std::mutex> lock(q->mutex);
  if (q->items.size() == q->capacity) return pdFALSE;
  const auto *b = static_cast<const unsigned char *>(p); q->items.emplace_back(b, b + q->itemSize); return pdTRUE;
}
inline BaseType_t xQueueReceive(QueueHandle_t q, void *p, TickType_t wait) {
  if (!q || wait) return pdFALSE;
  std::lock_guard<std::mutex> lock(q->mutex);
  if (q->items.empty()) return pdFALSE;
  memcpy(p, q->items.front().data(), q->itemSize); q->items.pop_front(); return pdTRUE;
}
