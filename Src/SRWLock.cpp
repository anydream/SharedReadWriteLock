#include "SRWLock.hpp"
#include "SRWInternals.hpp"

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
#pragma nounroll
		for (uint32_t spinCount = g_SRWSpinCount; spinCount; --spinCount)
		{
			if (!(static_cast<volatile const uint32_t&>(stackNode.Flags) & FLAG_SPINNING))
				break;
			PLATFORM_YIELD;
		}

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
bool SRWLock::try_lock()
{
	// 尝试设置锁定位
	return !Atomic::FetchBitSet(&LockStatus_, BIT_LOCKED);
}

void SRWLock::lock()
{
	// 成功获得锁时立即返回
	if (PLATFORM_LIKELY(try_lock()))
		return;

	uint32_t backoffCount = 0;
	alignas(16) SRWStackNode stackNode{};

	SRWStatus lastStatus = LockStatus_;

	for (;;)
	{
		if (lastStatus.Locked)
		{
			// 已锁定时进入等待模式
			if (TryWaiting<true>(&LockStatus_, stackNode, lastStatus))
			{
				lastStatus = LockStatus_;
				continue;
			}
		}
		else
		{
			// 尝试加锁, 成功后立即返回
			if (try_lock())
				return;
		}

		// 存在竞争时主动避让
		Backoff(&backoffCount);
		lastStatus = LockStatus_;
	}
}

void SRWLock::unlock()
{
	SRWStatus lastStatus = Atomic::CompareExchange<size_t>(&LockStatus_, FLAG_LOCKED, 0);
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

		SRWStatus currStatus = Atomic::CompareExchange<size_t>(&LockStatus_, lastStatus.Value, newStatus.Value);
		if (currStatus == lastStatus)
		{
			if (isWake)
				WakeUpLock(&LockStatus_, newStatus);
			return;
		}

		lastStatus = currStatus;
	}
}

bool SRWLock::try_lock_shared()
{
	// 未锁定时可以立即锁定
	SRWStatus lastStatus = Atomic::CompareExchange<size_t>(&LockStatus_, 0, FLAG_SHARED | FLAG_LOCKED);
	if (PLATFORM_LIKELY(lastStatus == 0))
		return true;

	uint32_t backoffCount = 0;

	for (;;)
	{
		// 已锁定, 且正在自旋或者非共享锁定时, 加锁失败
		if (lastStatus.Locked && (lastStatus.Spinning || !lastStatus.SharedCount))
			return false;

		// 尝试加锁
		if (TryLockShared(&LockStatus_, lastStatus))
			return true;

		// 存在竞争时主动避让
		Backoff(&backoffCount);
		lastStatus = LockStatus_;
	}
}

void SRWLock::lock_shared()
{
	// 未锁定时可以立即锁定
	SRWStatus lastStatus = Atomic::CompareExchange<size_t>(&LockStatus_, 0, FLAG_SHARED | FLAG_LOCKED);
	if (PLATFORM_LIKELY(lastStatus == 0))
		return;

	uint32_t backoffCount = 0;
	alignas(16) SRWStackNode stackNode{};

	for (;;)
	{
		if (lastStatus.Locked && (lastStatus.Spinning || !lastStatus.SharedCount))
		{
			// 已锁定, 且正在自旋或者非共享锁定时, 进入等待模式
			if (TryWaiting<false>(&LockStatus_, stackNode, lastStatus))
			{
				lastStatus = LockStatus_;
				continue;
			}
		}
		else
		{
			// 尝试加锁, 成功后立即返回
			if (TryLockShared(&LockStatus_, lastStatus))
				return;
		}

		// 存在竞争时主动避让
		Backoff(&backoffCount);
		lastStatus = LockStatus_;
	}
}

void SRWLock::unlock_shared()
{
	SRWStatus lastStatus = Atomic::CompareExchange<size_t>(&LockStatus_, FLAG_SHARED | FLAG_LOCKED, 0);
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

		newStatus = Atomic::CompareExchange<size_t>(&LockStatus_, lastStatus.Value, newStatus.Value);
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

		SRWStatus currStatus = Atomic::CompareExchange<size_t>(&LockStatus_, lastStatus.Value, newStatus.Value);
		if (currStatus == lastStatus)
		{
			if (isWake)
				WakeUpLock(&LockStatus_, newStatus);
			return;
		}

		lastStatus = currStatus;
	}
}
