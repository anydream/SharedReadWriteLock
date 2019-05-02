#pragma once

#include "Predefines.hpp"

#if defined(PLATFORM_IS_WINDOWS)
#elif defined(PLATFORM_IS_LINUX)
#else
#  include <mutex>
#  include <condition_variable>
#endif

//////////////////////////////////////////////////////////////////////////
class WaitEvent
{
public:
	void WaitMicrosec(uint64_t microsecs = -1);
	void WakeUp();

private:
#if defined(PLATFORM_IS_WINDOWS)
#elif defined(PLATFORM_IS_LINUX)
	int Futex_ = 0;
#else
	std::mutex Mutex_;
	std::condition_variable CondVar_;
	bool IsWakeUp_ = false;
#endif
};
