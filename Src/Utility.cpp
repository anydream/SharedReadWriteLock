#include "Utility.hpp"

#if defined(PLATFORM_IS_WINDOWS)
#  include <thread>
#  include <windows.h>
#else
#  include <time.h>
#endif

#if defined(PLATFORM_IS_APPLE)
#  include <sys/errno.h>
#  include <sys/time.h>
#  include <mach/clock.h>
#  include <mach/mach.h>
#  include <mach/mach_time.h>
#  if !defined(CLOCK_MONOTONIC)
#    define CLOCK_MONOTONIC (1)
#    define CLOCK_REALTIME (2)
#  endif

static uint64_t apple_gettick()
{
	static mach_timebase_info_data_t s_timeInfo = { 0, 0 };

	CALL_ONCE[]()
	{
		mach_timebase_info(&s_timeInfo);
	};

	uint64_t now = mach_absolute_time();
	now = now
		* s_timeInfo.numer
		/ s_timeInfo.denom;

	return now;
}
#endif

//////////////////////////////////////////////////////////////////////////
uint64_t GetTickNanosec()
{
#if defined(PLATFORM_IS_WINDOWS)
	static double s_freq = 0;
	static struct Initer
	{
		Initer()
		{
			LARGE_INTEGER li;
			QueryPerformanceFrequency(&li);
			s_freq = 1.0e9 / static_cast<double>(li.QuadPart);
		}
	} s_Initer;

	LARGE_INTEGER li;
	QueryPerformanceCounter(&li);
	return static_cast<uint64_t>(static_cast<double>(li.QuadPart) * s_freq);

#elif defined(PLATFORM_IS_APPLE)
	return apple_gettick();
#else
	struct timespec spec {};
	clock_gettime(CLOCK_MONOTONIC, &spec);
	return static_cast<uint64_t>(spec.tv_sec) * 1000000000 + static_cast<uint64_t>(spec.tv_nsec);
#endif
}

uint64_t GetTickMicrosec()
{
	return GetTickNanosec() / 1000;
}

uint64_t GetTickMillisec()
{
	return GetTickMicrosec() / 1000;
}
