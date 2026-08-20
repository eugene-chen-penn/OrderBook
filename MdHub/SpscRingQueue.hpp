#pragma once

#include <atomic>
#include <vector>
#include <cstdint>
#include <cstddef>


template <typename T>
class SpscRingQueue {
public:
    explicit SpscRingQueue(std::size_t capacity);
    bool push(const T& v);   // 生产者；满 → false（不阻塞、不覆盖）
    bool pop(T& out);        // 消费者；空 → false
    std::size_t capacity() const;
private:
    std::size_t capacity_;
    std::vector<T> data_;
    std::atomic<std::size_t> head;  // 生产者写入位置
    std::atomic<std::size_t> tail;  // 消费者读取位置
};