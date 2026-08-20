#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include "SpscRingQueue.hpp"

template <typename T>
SpscRingQueue<T>::SpscRingQueue(std::size_t capacity)
    : capacity_(capacity), data_(capacity), head(0), tail(0) {
    if (capacity == 0) {
        throw std::invalid_argument("queue capacity must be greater than zero");
    }
}

template <typename T>
bool SpscRingQueue<T>::push(const T& v) {
    const std::size_t current_head = head.load(std::memory_order_relaxed);
    const std::size_t current_tail = tail.load(std::memory_order_acquire);
    if (current_head - current_tail == capacity_) {
        return false; // Queue is full
    }
    data_[current_head % capacity_] = v;
    head.store(current_head + 1, std::memory_order_release);
    return true;
}

template <typename T>
bool SpscRingQueue<T>::pop(T& out) {
    const std::size_t current_tail = tail.load(std::memory_order_relaxed);
    const std::size_t current_head = head.load(std::memory_order_acquire);
    if (current_tail == current_head) {
        return false; // Queue is empty
    }
    out = data_[current_tail % capacity_];
    tail.store(current_tail + 1, std::memory_order_release);
    return true;
}

template <typename T>
std::size_t SpscRingQueue<T>::capacity() const {
    return capacity_;
}

template class SpscRingQueue<std::uint64_t>;
