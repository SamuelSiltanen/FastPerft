// Copyright 2022 Samuel Siltanen
// WorkQueue.cpp

#include "WorkQueue.hpp"
#include <cassert>
#include <malloc.h>

WorkQueue::WorkQueue(size_t initialCapacity)
    : m_front(0)
    , m_back(0)
    , m_capacity(1)
{
    while (m_capacity < initialCapacity) m_capacity *= 2;
    m_buffer = static_cast<WorkItem*>(_aligned_malloc(m_capacity * sizeof(WorkItem), alignof(WorkItem)));
}

WorkQueue::~WorkQueue()
{
    if (m_buffer)
    {
        _aligned_free(m_buffer);
        m_buffer = nullptr;
    }
}

// Doubles the capacity. Must be called with the lock held.
void WorkQueue::grow()
{
    size_t newCapacity = m_capacity * 2;
    WorkItem* newBuffer = static_cast<WorkItem*>(_aligned_malloc(newCapacity * sizeof(WorkItem), alignof(WorkItem)));
    for (int64_t i = m_front; i < m_back; ++i)
    {
        newBuffer[static_cast<uint64_t>(i) & (newCapacity - 1)] = slot(i);
    }
    _aligned_free(m_buffer);
    m_buffer = newBuffer;
    m_capacity = newCapacity;
}

void WorkQueue::push_back(const WorkItem& item)
{
    m_lock.lock();
    if (static_cast<size_t>(m_back - m_front) == m_capacity) grow();
    slot(m_back) = item;
    item.result->workLeft++;
    m_back++;
    m_lock.unlock();
}

void WorkQueue::push_front(const WorkItem& item)
{
    m_lock.lock();
    push_front_unsafe(item);
    m_lock.unlock();
}

void WorkQueue::push_front_unsafe(const WorkItem& item)
{
    if (static_cast<size_t>(m_back - m_front) == m_capacity) grow();
    m_front--;
    slot(m_front) = item;
    item.result->workLeft++;
}

bool WorkQueue::try_pop_front(WorkItem& item)
{    
    m_lock.lock();
    if (m_front == m_back)
    {
        m_lock.unlock();
        return false;
    }
    item = slot(m_front);
    m_front++;
    m_lock.unlock();
    return true;
}

// Pops only items that were pushed to the front after the marker was taken
bool WorkQueue::try_pop_front(WorkItem& item, int64_t marker)
{
    m_lock.lock();
    if (m_front == m_back || m_front >= marker)
    {
        m_lock.unlock();
        return false;
    }
    item = slot(m_front);
    m_front++;
    m_lock.unlock();
    return true;
}

void WorkQueue::lock()
{
    m_lock.lock();
}

void WorkQueue::unlock()
{
    m_lock.unlock();
}

int64_t WorkQueue::marker()
{
    return m_front;
}
