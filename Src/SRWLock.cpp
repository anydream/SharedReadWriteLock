#include "SRWLock.hpp"
#include "Atomic.hpp"
#include "WaitEvent.hpp"
#include "Utility.hpp"
#include "DebugLog.hpp"

//////////////////////////////////////////////////////////////////////////
// 状态位顺序
enum SRWBits
{
	// 锁定中. 独占或共享锁定
	BIT_LOCKED = 0,
	// 自旋中. 正在等待
	BIT_SPINNING = 1,
	// 唤醒中或优化等待链表. 只有存在自旋位时才能设置
	BIT_WAKING = 2,
	// 多重共享者
	BIT_MULTI_SHARED = 3,
	// 共享计数位
	BIT_SHARED = 4,
};

// 状态位标记
enum SRWFlags
{
	FLAG_LOCKED = 1 << BIT_LOCKED,
	FLAG_SPINNING = 1 << BIT_SPINNING,
	FLAG_WAKING = 1 << BIT_WAKING,
	FLAG_MULTI_SHARED = 1 << BIT_MULTI_SHARED,
	FLAG_SHARED = 1 << BIT_SHARED,
};

// 栈节点. 锁争用时, 等待者使用链表串联各个线程栈上的节点
struct SRWStackNode : WaitEvent
{
	// 上一节点
	SRWStackNode *Back;
	// 通知节点
	SRWStackNode *Notify;
	// 下一节点
	SRWStackNode *Next;
	// 共享计数
	uint32_t SharedCount;
	// 线程标记, 值为 FLAG_LOCKED 或 FLAG_SPINNING
	uint32_t Flags;
};

// 锁状态
struct SRWStatus
{
	union
	{
		struct
		{
			size_t Locked : 1;
			size_t Spinning : 1;
			size_t Waking : 1;
			size_t MultiShared : 1;
			size_t SharedCount : sizeof(size_t) * 8 - 4;
		};
		size_t Value;
	};

	SRWStatus() : Value(0)
	{
	}

	SRWStatus(size_t value) : Value(value)
	{
	}

	bool operator ==(size_t value) const
	{
		return Value == value;
	}

	bool operator ==(const SRWStatus &other) const
	{
		return Value == other.Value;
	}

	bool operator !=(size_t value) const
	{
		return !(*this == value);
	}

	bool operator !=(const SRWStatus &other) const
	{
		return !(*this == other);
	}

	SRWStackNode* WaitNode() const
	{
		return reinterpret_cast<SRWStackNode*>(Value & ~(FLAG_MULTI_SHARED | FLAG_WAKING | FLAG_SPINNING | FLAG_LOCKED));
	}

	size_t WithoutMultiSharedLocked() const
	{
		return Value & ~(FLAG_MULTI_SHARED | FLAG_LOCKED);
	}
};

//////////////////////////////////////////////////////////////////////////
static uint32_t g_SRWSpinCount = 1024;

static void Backoff(uint32_t *pCount)
{
	uint32_t count = *pCount;
	if (count)
	{
		if (count < 0x1FFF)
			count <<= 1;
	}
	else
	{
		// TODO: 单核心直接返回
		// 设置初始次数
		count = 64;
	}

	*pCount = count;
	// 生成随机退让次数
	count += (count - 1) & RandomValue();

#pragma nounroll
	while (count--)
		PLATFORM_YIELD;
}

// 查找通知节点并连接
static SRWStackNode* FindNotifyNode(SRWStackNode *pWaitNode)
{
	SRWStackNode *pCurr = pWaitNode;

	for (;;)
	{
		SRWStackNode *pNotify = pCurr->Notify;
		if (pNotify)
			return pNotify;

		SRWStackNode *pLast = pCurr;
		pCurr = pCurr->Back;
		pCurr->Next = pLast;
	}
}

// 尝试清除唤醒状态
static bool TryClearWaking(size_t *pLockStatus, SRWStatus &lastStatus)
{
	SRWStatus newStatus = lastStatus.Value - FLAG_WAKING;
	AssertDebug(!newStatus.Waking);
	AssertDebug(newStatus.Locked);

	newStatus = Atomic::CompareExchange<size_t>(pLockStatus, lastStatus.Value, newStatus.Value);
	if (lastStatus == newStatus)
		return true;

	lastStatus = newStatus;
	return false;
}

PLATFORM_NOINLINE static void WakeUpLock(size_t *pLockStatus, SRWStatus lastStatus)
{
	SRWStackNode *pNotify;
	for (;;)
	{
		AssertDebug(!lastStatus.MultiShared);

		// 锁定状态尝试清除唤醒标记
		while (lastStatus.Locked)
		{
			AssertDebug(lastStatus.Spinning);
			if (TryClearWaking(pLockStatus, lastStatus))
				return;
		}

		// 寻找需要通知的节点
		SRWStackNode *pWaitNode = lastStatus.WaitNode();
		pNotify = FindNotifyNode(pWaitNode);

		SRWStackNode *pNext = pNotify->Next;
		if ((pNotify->Flags & FLAG_LOCKED) && pNext)
		{
			// 如果通知存在下一个节点, 并且通知包含锁状态, 则更新下一通知节点并唤醒当前节点
			pWaitNode->Notify = pNext;
			pNotify->Next = nullptr;

			AssertDebug(pWaitNode != pNotify);
			AssertDebug(SRWStatus(*pLockStatus).Spinning);

			// 尝试清除唤醒标记
			Atomic::FetchAnd<size_t>(pLockStatus, ~FLAG_WAKING);
			break;
		}
		else
		{
			// 尝试清除所有状态
			SRWStatus newStatus = Atomic::CompareExchange<size_t>(pLockStatus, lastStatus.Value, 0);
			if (lastStatus == newStatus)
				break;

			lastStatus = newStatus;
		}
	}

	// 唤醒需要通知的节点
	do
	{
		// 正向遍历通知节点链表
		SRWStackNode *pNext = pNotify->Next;

		// 尝试清除自旋标记
		if (!Atomic::FetchBitClear(&pNotify->Flags, BIT_SPINNING))
		{
			// 如果之前不在自旋则唤醒
			pNotify->WakeUp();
		}

		pNotify = pNext;
	} while (pNotify);
}

void OptimizeLockList(size_t *pLockStatus, SRWStatus lastStatus)
{
	// 锁定状态时循环
	while (lastStatus.Locked)
	{
		// 更新通知节点
		SRWStackNode *pWaitNode = lastStatus.WaitNode();
		SRWStackNode *pNotify = FindNotifyNode(pWaitNode);
		pWaitNode->Notify = pNotify;

		// 尝试清除唤醒标记
		if (TryClearWaking(pLockStatus, lastStatus))
			return;
	}
	// 如果是无锁定状态则唤醒
	WakeUpLock(pLockStatus, lastStatus);
}

template <bool IsExclusive>
PLATFORM_NOINLINE static bool TryWaiting(size_t *pLockStatus, SRWStackNode &stackNode, SRWStatus &lastStatus)
{
	SRWStatus newStatus;
	bool isOptimize = false;

	// 当前线程标记为自旋和锁定
	if (IsExclusive)
		stackNode.Flags = FLAG_SPINNING | FLAG_LOCKED;
	else
		stackNode.Flags = FLAG_SPINNING;

	stackNode.SharedCount = 0;
	stackNode.Next = nullptr;

	if (lastStatus.Spinning)
	{
		// 其他线程正在自旋
		stackNode.Notify = nullptr;
		// 作为当前线程的上一个节点挂接
		stackNode.Back = lastStatus.WaitNode();
		// 继承多重共享标记, 并设置唤醒, 自旋和锁定和标记
		newStatus = reinterpret_cast<size_t>(&stackNode) | (lastStatus.Value & FLAG_MULTI_SHARED) | FLAG_WAKING | FLAG_SPINNING | FLAG_LOCKED;

		// 不包含唤醒标记的情况需要尝试优化链表
		if (!lastStatus.Waking)
			isOptimize = true;
	}
	else
	{
		// 把当前线程作为下一个通知节点
		stackNode.Notify = &stackNode;
		newStatus = reinterpret_cast<size_t>(&stackNode) | FLAG_SPINNING | FLAG_LOCKED;

		if (IsExclusive)
		{
			stackNode.SharedCount = lastStatus.SharedCount;
			// 共享个数大于 1 的情况需要设置多重共享标记
			if (stackNode.SharedCount > 1)
				newStatus.MultiShared = 1;
		}
	}

	AssertDebug(newStatus.Spinning);
	AssertDebug(newStatus.Locked);
	AssertDebug(lastStatus.Locked);

	// 尝试更新锁状态
	if (lastStatus == Atomic::CompareExchange<size_t>(pLockStatus, lastStatus.Value, newStatus.Value))
	{
		// 更新成功, 优化链表
		if (isOptimize)
			OptimizeLockList(pLockStatus, newStatus);

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
			stackNode.WaitMicrosec();

		return true;
	}
	return false;
}

template <bool IsExclusive>
static bool TrySetLock(size_t *pLockStatus, SRWStatus lastStatus)
{
	// 无锁定时则尝试加锁
	if (IsExclusive)
	{
		return Atomic::FetchBitSet(pLockStatus, BIT_LOCKED) == false;
	}
	else
	{
		SRWStatus newStatus;
		// 共享状态尝试增加计数
		if (lastStatus.Spinning)
			newStatus = lastStatus.Value | FLAG_LOCKED;
		else
			newStatus = (lastStatus.Value + FLAG_SHARED) | FLAG_LOCKED;

		// 状态更新成功表示加锁成功
		return lastStatus == Atomic::CompareExchange<size_t>(pLockStatus, lastStatus.Value, newStatus.Value);
	}
}

//////////////////////////////////////////////////////////////////////////
bool SRWLock::try_lock()
{
	// 尝试设置锁定位
	return Atomic::FetchBitSet(&LockStatus_, BIT_LOCKED) == false;
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
			if (TrySetLock<true>(&LockStatus_, lastStatus))
				return;
		}

		// 存在竞争时主动避让
		Backoff(&backoffCount);
		// prefetchw byte ptr[LockStatus_] }
		lastStatus = LockStatus_;
	}
}

void SRWLock::unlock()
{
	// 去除锁标记
	SRWStatus lastStatus = Atomic::FetchAdd<size_t>(&LockStatus_, -FLAG_LOCKED);
	AssertDebug(lastStatus.Locked);
	AssertDebug(lastStatus.Spinning || !lastStatus.SharedCount);

	// 如果正在自旋且没有唤醒则尝试唤醒
	if (PLATFORM_UNLIKELY(lastStatus.Spinning && !lastStatus.Waking))
	{
		lastStatus.Locked = 0;
		// 添加唤醒标记
		SRWStatus newStatus = lastStatus.Value + FLAG_WAKING;
		// 更新状态成功后执行唤醒
		if (lastStatus == Atomic::CompareExchange<size_t>(&LockStatus_, lastStatus.Value, newStatus.Value))
			WakeUpLock(&LockStatus_, newStatus);
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
		if (TrySetLock<false>(&LockStatus_, lastStatus))
			return true;

		// 存在竞争时主动避让
		Backoff(&backoffCount);
		// prefetchw byte ptr[LockStatus_] }
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
			if (TrySetLock<false>(&LockStatus_, lastStatus))
				return;
		}

		// 存在竞争时主动避让
		Backoff(&backoffCount);
		// prefetchw byte ptr[LockStatus_] }
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
		if (lastStatus == newStatus)
			return;

		lastStatus = newStatus;
	}

	if (lastStatus.MultiShared)
	{
		SRWStackNode *pWaitNode = lastStatus.WaitNode();
		SRWStackNode *pNotify;
		for (;;)
		{
			pNotify = pWaitNode->Notify;
			if (pNotify)
				break;
			pWaitNode = pWaitNode->Back;
		}

		AssertDebug(pNotify->SharedCount);
		AssertDebug(pNotify->Flags & FLAG_LOCKED);

		if (Atomic::DecrementFetch(&pNotify->SharedCount) > 0)
			return;
	}

	for (;;)
	{
		if (lastStatus.Waking)
		{
			SRWStatus newStatus = lastStatus.WithoutMultiSharedLocked();

			newStatus = Atomic::CompareExchange<size_t>(&LockStatus_, lastStatus.Value, newStatus.Value);
			if (lastStatus == newStatus)
				return;

			lastStatus = newStatus;
		}
		else
		{
			SRWStatus oldStatus = lastStatus;
			SRWStatus newStatus = oldStatus.WithoutMultiSharedLocked();
			newStatus.Waking = 1;

			lastStatus = Atomic::CompareExchange<size_t>(&LockStatus_, oldStatus.Value, newStatus.Value);
			if (lastStatus == oldStatus)
			{
				WakeUpLock(&LockStatus_, newStatus);
				return;
			}
		}
	}
}
