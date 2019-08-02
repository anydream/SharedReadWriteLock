#include "SRWLock.hpp"
#include "SRWInternals.hpp"
#include <thread>

#if defined(PLATFORM_ARCH_X86) && defined(PLATFORM_GNUC_LIKE)
#  include <cpuid.h>
#endif

//////////////////////////////////////////////////////////////////////////
static uint32_t g_CyclesPerYield = 10;
static uint32_t g_ProcessorThreads = 1;

void SRWLock_Init()
{
	g_ProcessorThreads = std::thread::hardware_concurrency();

#if defined(PLATFORM_ARCH_X86)
#  if defined(PLATFORM_GNUC_LIKE)
	uint32_t cpuInfo[4];
	__get_cpuid(0, &cpuInfo[0], &cpuInfo[1], &cpuInfo[2], &cpuInfo[3]);
#  else
	int cpuInfo[4];
	__cpuid(cpuInfo, 0);
#  endif
	if (cpuInfo[0] == 0x16)
	{
		// Skylake 修改了 pause 指令的耗时
		g_CyclesPerYield = 140;
		return;
	}
#endif
	g_CyclesPerYield = 10;
}

static struct Init
{
	Init()
	{
		SRWLock_Init();
	}
} g_Init;

//////////////////////////////////////////////////////////////////////////
static uint32_t RandomValue()
{
#if defined(PLATFORM_ARCH_X86)
#  if defined(PLATFORM_MSVC_LIKE)
	return static_cast<uint32_t>(__rdtsc());
#  else
	return static_cast<uint32_t>(__builtin_ia32_rdtsc());
#  endif
#else
	return rand();
#endif
}

PLATFORM_NOINLINE void Backoff(uint32_t *pCount)
{
	uint32_t count = *pCount;
	if (count)
	{
		if (count < 0x1FFF)
			count *= 2;
	}
	else
	{
		// 单核心直接返回
		if (g_ProcessorThreads == 1)
			return;

		// 设置初始次数
		count = 64;
	}

	*pCount = count;
	// 生成随机退让次数
	count = 10 * ((count - 1) & RandomValue() + count) / g_CyclesPerYield;

#pragma nounroll
	while (count--)
		PLATFORM_YIELD;
}

void Spinning(SRWStackNode &stackNode)
{
	// 单核心直接返回
	if (g_ProcessorThreads == 1)
		return;

#pragma nounroll
	for (uint32_t spinCount = 10500 / g_CyclesPerYield; spinCount; --spinCount)
	{
		if (!(static_cast<volatile const uint32_t&>(stackNode.Flags) & FLAG_SPINNING))
			break;
		PLATFORM_YIELD;
	}
}

//////////////////////////////////////////////////////////////////////////
template <bool IsExclusive>
PLATFORM_NOINLINE static bool TryWaiting(size_t *pLockStatus, SRWStackNode &stackNode, SRWStatus lastStatus)
{
	// 当前线程标记为自旋和锁定
	if (IsExclusive)
		stackNode.Flags = FLAG_SPINNING | FLAG_LOCKED;
	else
		stackNode.Flags = FLAG_SPINNING;

	// 尝试更新锁状态
	if (QueueStackNode<IsExclusive>(pLockStatus, &stackNode, lastStatus))
	{
		// 自旋一定次数再睡眠
		Spinning(stackNode);

		// 成功清除自旋状态时进入睡眠状态
		if (Atomic::FetchBitClear(&stackNode.Flags, BIT_SPINNING))
		{
			do
			{
				stackNode.WaitMicrosec();
			} while (!(stackNode.Flags & FLAG_WAKING));
		}

		return true;
	}
	return false;
}

static bool TryLockShared(size_t *pLockStatus, SRWStatus lastStatus)
{
	SRWStatus newStatus = lastStatus.Value | FLAG_LOCKED;
	// 共享状态尝试增加计数
	if (!lastStatus.Spinning)
		newStatus = newStatus.Value + FLAG_SHARED;

	// 状态更新成功表示加锁成功
	return lastStatus == Atomic::CompareExchange<size_t>(pLockStatus, lastStatus.Value, newStatus.Value);
}

//////////////////////////////////////////////////////////////////////////
bool SRWLock_TryLock(size_t *pLockStatus)
{
	// 尝试设置锁定位
	return !Atomic::FetchBitSet(pLockStatus, BIT_LOCKED);
}

void SRWLock_Lock(size_t *pLockStatus)
{
	// 成功获得锁时立即返回
	if (PLATFORM_LIKELY(SRWLock_TryLock(pLockStatus)))
		return;

	uint32_t backoffCount = 0;
	alignas(16) SRWStackNode stackNode{};

	SRWStatus lastStatus = *pLockStatus;

	for (;;)
	{
		if (lastStatus.Locked)
		{
			// 已锁定时进入等待模式
			if (TryWaiting<true>(pLockStatus, stackNode, lastStatus))
			{
				lastStatus = *pLockStatus;
				continue;
			}
		}
		else
		{
			// 尝试加锁, 成功后立即返回
			if (SRWLock_TryLock(pLockStatus))
				return;
		}

		// 存在竞争时主动避让
		Backoff(&backoffCount);
		lastStatus = *pLockStatus;
	}
}

void SRWLock_Unlock(size_t *pLockStatus)
{
	SRWStatus lastStatus = Atomic::CompareExchange<size_t>(pLockStatus, FLAG_LOCKED, 0);
	if (PLATFORM_LIKELY(lastStatus == FLAG_LOCKED))
		return;

	for (;;)
	{
		SRWStatus newStatus = lastStatus;
		newStatus.Locked = 0;

		bool isWake = false;
		if (lastStatus.Spinning && !lastStatus.Waking)
		{
			newStatus.Waking = 1;
			isWake = true;
		}

		SRWStatus currStatus = Atomic::CompareExchange<size_t>(pLockStatus, lastStatus.Value, newStatus.Value);
		if (currStatus == lastStatus)
		{
			if (isWake)
				WakeUpLock(pLockStatus, newStatus);
			return;
		}

		lastStatus = currStatus;
	}
}

bool SRWLock_TryLockShared(size_t *pLockStatus)
{
	// 未锁定时可以立即锁定
	SRWStatus lastStatus = Atomic::CompareExchange<size_t>(pLockStatus, 0, FLAG_SHARED | FLAG_LOCKED);
	if (PLATFORM_LIKELY(lastStatus == 0))
		return true;

	uint32_t backoffCount = 0;

	for (;;)
	{
		// 已锁定, 且正在自旋或者非共享锁定时, 加锁失败
		if (lastStatus.Locked && (lastStatus.Spinning || !lastStatus.SharedCount))
			return false;

		// 尝试加锁
		if (TryLockShared(pLockStatus, lastStatus))
			return true;

		// 存在竞争时主动避让
		Backoff(&backoffCount);
		lastStatus = *pLockStatus;
	}
}

void SRWLock_LockShared(size_t *pLockStatus)
{
	// 未锁定时可以立即锁定
	SRWStatus lastStatus = Atomic::CompareExchange<size_t>(pLockStatus, 0, FLAG_SHARED | FLAG_LOCKED);
	if (PLATFORM_LIKELY(lastStatus == 0))
		return;

	uint32_t backoffCount = 0;
	alignas(16) SRWStackNode stackNode{};

	for (;;)
	{
		if (lastStatus.Locked && (lastStatus.Spinning || !lastStatus.SharedCount))
		{
			// 已锁定, 且正在自旋或者非共享锁定时, 进入等待模式
			if (TryWaiting<false>(pLockStatus, stackNode, lastStatus))
			{
				lastStatus = *pLockStatus;
				continue;
			}
		}
		else
		{
			// 尝试加锁, 成功后立即返回
			if (TryLockShared(pLockStatus, lastStatus))
				return;
		}

		// 存在竞争时主动避让
		Backoff(&backoffCount);
		lastStatus = *pLockStatus;
	}
}

void SRWLock_UnlockShared(size_t *pLockStatus)
{
	SRWStatus lastStatus = Atomic::CompareExchange<size_t>(pLockStatus, FLAG_SHARED | FLAG_LOCKED, 0);
	if (PLATFORM_LIKELY(lastStatus == (FLAG_SHARED | FLAG_LOCKED)))
		return;

	AssertDebug(lastStatus.Locked);

	while (!lastStatus.Spinning)
	{
		SRWStatus newStatus;

		// 共享计数减一
		if (lastStatus.SharedCount > 1)
			newStatus = lastStatus.Value - FLAG_SHARED;
		else
			newStatus = 0;

		newStatus = Atomic::CompareExchange<size_t>(pLockStatus, lastStatus.Value, newStatus.Value);
		if (newStatus == lastStatus)
			return;

		lastStatus = newStatus;
	}

	if (lastStatus.MultiShared)
	{
		SRWStackNode *pCurr = lastStatus.WaitNode();
		SRWStackNode *pNotify;
		for (;;)
		{
			pNotify = pCurr->Notify;
			if (pNotify)
				break;
			pCurr = pCurr->Back;
		}

		AssertDebug(pNotify->SharedCount);
		AssertDebug(pNotify->Flags & FLAG_LOCKED);

		if (Atomic::DecrementFetch(&pNotify->SharedCount) > 0)
			return;
	}

	for (;;)
	{
		SRWStatus newStatus = lastStatus.WithoutMultiSharedLocked();

		bool isWake = false;
		if (lastStatus.Spinning && !lastStatus.Waking)
		{
			newStatus.Waking = 1;
			isWake = true;
		}

		SRWStatus currStatus = Atomic::CompareExchange<size_t>(pLockStatus, lastStatus.Value, newStatus.Value);
		if (currStatus == lastStatus)
		{
			if (isWake)
				WakeUpLock(pLockStatus, newStatus);
			return;
		}

		lastStatus = currStatus;
	}
}

//////////////////////////////////////////////////////////////////////////
bool SRWLock::try_lock()
{
	return SRWLock_TryLock(&LockStatus_);
}

void SRWLock::lock()
{
	SRWLock_Lock(&LockStatus_);
}

void SRWLock::unlock()
{
	SRWLock_Unlock(&LockStatus_);
}

bool SRWLock::try_lock_shared()
{
	return SRWLock_TryLockShared(&LockStatus_);
}

void SRWLock::lock_shared()
{
	SRWLock_LockShared(&LockStatus_);
}

void SRWLock::unlock_shared()
{
	SRWLock_UnlockShared(&LockStatus_);
}

//////////////////////////////////////////////////////////////////////////
#if defined(PLATFORM_IS_WINDOWS)
#  include <windows.h>
#elif defined(PLATFORM_IS_UNIX) || defined(PLATFORM_IS_APPLE)
#  include <sched.h>
#  include <unistd.h>
#  define PLATFORM_IS_UNIX_OR_APPLE

#  if defined(PLATFORM_IS_APPLE)
#    include <pthread.h>
#  else
#    include <sys/syscall.h>
#  endif
#endif

static uint32_t GetThreadIDImpl()
{
#if defined(PLATFORM_IS_WINDOWS)
	return static_cast<uint32_t>(GetCurrentThreadId());
#elif defined(PLATFORM_IS_UNIX)
	return static_cast<uint32_t>(syscall(SYS_gettid));
#elif defined(PLATFORM_IS_APPLE)
	uint64_t tid = 0;
	pthread_threadid_np(nullptr, &tid);
	return static_cast<uint32_t>(tid);
#endif
}

//////////////////////////////////////////////////////////////////////////
void SRWRecLock::lock()
{
	uint32_t currTID = GetThreadIDImpl();

	if (ThreadID_ != currTID)
		Lock_.lock();

	if (++RecCount_ == 1)
		ThreadID_ = currTID;
}

bool SRWRecLock::try_lock()
{
	uint32_t currTID = GetThreadIDImpl();

	bool isAcquired;
	if (ThreadID_ != currTID)
		isAcquired = Lock_.try_lock();
	else
		isAcquired = true;

	if (isAcquired)
	{
		if (++RecCount_ == 1)
			ThreadID_ = currTID;
	}

	return isAcquired;
}

void SRWRecLock::unlock()
{
	AssertDebug(
		RecCount_ >= 1 &&
		ThreadID_ == GetThreadIDImpl());

	if (--RecCount_ == 0)
	{
		ThreadID_ = -1;
		Lock_.unlock();
	}
}
