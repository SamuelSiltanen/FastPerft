// Copyright 2022 Samuel Siltanen
// WorkQueue.cpp

#include "WorkQueue.hpp"
#include <cassert>

WorkQueue::WorkQueue(size_t size)
    : m_front(0)
    , m_back(0)
    , m_size(size)
    , m_elements(0)
{
    m_buffer = new WorkItem[size];
}

WorkQueue::~WorkQueue()
{
    if (m_buffer)
    {
        delete[] m_buffer;
        m_buffer = nullptr;
    }
}

void WorkQueue::push_back(const WorkItem& item)
{
    m_lock.lock();    
    m_buffer[m_back] = item;
    m_buffer[m_back].result->workLeft++;
    m_back = (m_back + 1) % m_size;
    m_elements++;
    assert(m_elements <= m_size);
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
    m_front = (m_front + m_size - 1) % m_size;
    m_buffer[m_front] = item;
    m_buffer[m_front].result->workLeft++;
    m_elements++;
    assert(m_elements <= m_size);
}

bool WorkQueue::try_pop_front(WorkItem& item)
{    
    m_lock.lock();
    assert(m_elements >= 0);
    if (m_elements == 0)
    {
        m_lock.unlock();
        return false;
    }
    item = m_buffer[m_front];
    m_front = (m_front + 1) % m_size;
    m_elements--;
    m_lock.unlock();
    return true;
}

bool WorkQueue::try_pop_front(WorkItem& item, size_t marker)
{
    m_lock.lock();
    assert(m_elements >= 0);
    if (m_elements == 0)
    {
        m_lock.unlock();
        return false;
    }
    
    if (((marker <= m_back) && (m_front >= marker) && (m_front < m_back)) ||
        ((m_back < marker) && ((m_front >= marker) || (m_front < m_back))))
    {
        m_lock.unlock();
        return false;
    }

    item = m_buffer[m_front];
    m_front = (m_front + 1) % m_size;
    m_elements--;
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

size_t WorkQueue::marker()
{
    return m_front;
}
