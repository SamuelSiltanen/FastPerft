// Copyright 2022 Samuel Siltanen
// WorkQueue.hpp

#pragma once

#include <mutex>
#include <atomic>

#include "ChessTypes.hpp"

struct WorkResult
{
    std::atomic<uint64_t> count;
    std::atomic<int> workLeft;
};

struct alignas(64) WorkItem
{
    Position pos;
    int depth;

    WorkResult* result;
};

// A double-ended work queue that grows when it is full.
// The front and back are logical indices that are never wrapped around, so that markers
// (logical front indices) stay valid when the buffer grows. The buffer slot of a logical
// index is the index modulo the capacity, which is a power of two.
class WorkQueue
{
public:
    WorkQueue(size_t initialCapacity);
    ~WorkQueue();

    WorkQueue(WorkQueue&) = delete;
    WorkQueue(WorkQueue&&) = delete;
    const WorkQueue& operator=(WorkQueue&) = delete;
    const WorkQueue& operator=(WorkQueue&&) = delete;

    void push_back(const WorkItem& item);
    void push_front(const WorkItem& item);
    void push_front_unsafe(const WorkItem& item);

    bool try_pop_front(WorkItem& item);
    bool try_pop_front(WorkItem& item, int64_t marker);

    void lock();
    void unlock();

    int64_t marker();
private:
    void grow();
    WorkItem& slot(int64_t index) { return m_buffer[static_cast<uint64_t>(index) & (m_capacity - 1)]; }

    WorkItem* m_buffer;
    int64_t m_front;
    int64_t m_back;
    size_t m_capacity;

    std::mutex m_lock;
};
