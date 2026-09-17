#include "main_thread_queue.h"

#include "extension.h"

static void OnFrame(void *data)
{
	MainThreadQueue *queue = static_cast<MainThreadQueue *>(data);
	std::vector<TransferEvent> events = queue->Drain();
	for (const TransferEvent &event : events)
	{
		g_S3Extension.HandleEvent(event);
	}
}

void MainThreadQueue::Post(TransferEvent &&event)
{
	if (!m_enabled)
	{
		return;
	}

	bool schedule = false;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_events.push_back(std::move(event));
		if (!m_scheduled)
		{
			m_scheduled = true;
			schedule = true;
		}
	}
	if (schedule)
	{
		smutils->AddFrameAction(OnFrame, this);
	}
}

std::vector<TransferEvent> MainThreadQueue::Drain()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	std::vector<TransferEvent> events;
	events.swap(m_events);
	m_scheduled = false;
	return events;
}

void MainThreadQueue::SetEnabled(bool enabled)
{
	m_enabled = enabled;
}
