#include "WaitEvent.hpp"
#include "DebugLog.hpp"

//////////////////////////////////////////////////////////////////////////
#if defined(PLATFORM_IS_WINDOWS)
#  include <windows.h>

static NTSTATUS (__stdcall *NtCreateKeyedEvent)(
	PHANDLE KeyedEventHandle,
	ACCESS_MASK DesiredAccess,
	void *ObjectAttributes,
	ULONG Reserved) = nullptr;

static NTSTATUS (__stdcall *NtWaitForKeyedEvent)(
	HANDLE KeyedEventHandle,
	PVOID Key,
	BOOLEAN Alertable,
	PLARGE_INTEGER Timeout) = nullptr;

static NTSTATUS (__stdcall *NtReleaseKeyedEvent)(
	HANDLE KeyedEventHandle,
	PVOID Key,
	BOOLEAN Alertable,
	PLARGE_INTEGER Timeout) = nullptr;

static struct KeyedEvent
{
	HANDLE Handle_;

	KeyedEvent()
	{
		HMODULE hDLL = GetModuleHandleW(L"ntdll");
		Assert(hDLL);

		NtCreateKeyedEvent = reinterpret_cast<decltype(NtCreateKeyedEvent)>(GetProcAddress(hDLL, "NtCreateKeyedEvent"));
		NtWaitForKeyedEvent = reinterpret_cast<decltype(NtWaitForKeyedEvent)>(GetProcAddress(hDLL, "NtWaitForKeyedEvent"));
		NtReleaseKeyedEvent = reinterpret_cast<decltype(NtReleaseKeyedEvent)>(GetProcAddress(hDLL, "NtReleaseKeyedEvent"));

		Assert(NtCreateKeyedEvent);
		Assert(NtWaitForKeyedEvent);
		Assert(NtReleaseKeyedEvent);

		NtCreateKeyedEvent(&Handle_, EVENT_ALL_ACCESS, nullptr, 0);
	}

	~KeyedEvent()
	{
		CloseHandle(Handle_);
	}
} g_KeyedEvent;

#elif defined(PLATFORM_IS_LINUX)
#  include <unistd.h>
#  include <sys/time.h>
#  include <sys/syscall.h>
#  include <linux/futex.h>
#endif

//////////////////////////////////////////////////////////////////////////
void WaitEvent::WaitMicrosec(uint64_t microsecs)
{
#if defined(PLATFORM_IS_WINDOWS)
	if (microsecs == -1)
		NtWaitForKeyedEvent(g_KeyedEvent.Handle_, this, false, nullptr);
	else
	{
		LARGE_INTEGER timeOut;
		timeOut.QuadPart = -static_cast<int64_t>(microsecs * 10);
		NtWaitForKeyedEvent(g_KeyedEvent.Handle_, this, false, &timeOut);
	}
#elif defined(PLATFORM_IS_LINUX)
	if (microsecs == -1)
		syscall(SYS_futex, &Futex_, FUTEX_WAIT_PRIVATE, Futex_, nullptr, 0, 0);
	else
	{
		timespec ts;
		ts.tv_sec = microsecs / 1000000;
		ts.tv_nsec = (microsecs % 1000000) * 1000;
		syscall(SYS_futex, &Futex_, FUTEX_WAIT_PRIVATE, Futex_, &ts, 0, 0);
	}
#else
	std::unique_lock<std::mutex> lk(Mutex_);
	if (microsecs == -1)
		CondVar_.wait(lk, [this] { return IsWakeUp_; });
	else
		CondVar_.wait_for(lk, std::chrono::microseconds(microsecs), [this] { return IsWakeUp_; });
	IsWakeUp_ = false;
#endif
}

void WaitEvent::WakeUp()
{
#if defined(PLATFORM_IS_WINDOWS)
	NtReleaseKeyedEvent(g_KeyedEvent.Handle_, this, false, nullptr);
#elif defined(PLATFORM_IS_LINUX)
#  pragma nounroll
	while (syscall(SYS_futex, &Futex_, FUTEX_WAKE_PRIVATE, 1, 0, 0, 0) != 1)
	{
		// 唤醒失败时退让
#  pragma nounroll
		for (int i = 0; i < 64; ++i)
			PLATFORM_YIELD;
	}
#else
	{
		std::lock_guard<std::mutex> lk(Mutex_);
		IsWakeUp_ = true;
	}
	CondVar_.notify_one();
#endif
}
