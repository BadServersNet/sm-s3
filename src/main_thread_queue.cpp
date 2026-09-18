#include "main_thread_queue.h"

void MainThreadQueue::Post(TransferEvent &&event)
{
	if (!m_enabled)
	{
		return;
	}

	std::lock_guard<std::mutex> const lock(m_mutex);
	m_events.push_back(std::move(event));
	m_hasEvents = true;
}

std::vector<TransferEvent> MainThreadQueue::Drain()
{
	std::lock_guard<std::mutex> const lock(m_mutex);
	std::vector<TransferEvent> events;
	events.swap(m_events);
	m_hasEvents = false;

	return events;
}

void MainThreadQueue::SetEnabled(bool enabled)
{
	m_enabled = enabled;
}
