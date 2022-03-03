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

class WorkQueue
{
public:
    WorkQueue(size_t size);
    ~WorkQueue();

    WorkQueue(WorkQueue&) = delete;
    WorkQueue(WorkQueue&&) = delete;
    const WorkQueue& operator=(WorkQueue&) = delete;
    const WorkQueue& operator=(WorkQueue&&) = delete;

    void push_back(const WorkItem& item);
    void push_front(const WorkItem& item);
    void push_front_unsafe(const WorkItem& item);

    bool try_pop_front(WorkItem& item);
    bool try_pop_front(WorkItem& item, size_t marker);

    void lock();
    void unlock();

    size_t marker();
private:
    WorkItem* m_buffer;
    size_t m_front;
    size_t m_back;
    size_t m_size;
    size_t m_elements;

    std::mutex m_lock;
};
