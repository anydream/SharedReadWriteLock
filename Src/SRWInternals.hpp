#pragma once

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
	FLAG_ALL = FLAG_MULTI_SHARED | FLAG_WAKING | FLAG_SPINNING | FLAG_LOCKED
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
	// 线程标记, 值为 FLAG_LOCKED, FLAG_SPINNING 或 FLAG_WAKING
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
		return reinterpret_cast<SRWStackNode*>(Value & ~FLAG_ALL);
	}

	size_t WithoutMultiSharedLocked() const
	{
		return Value & ~(FLAG_MULTI_SHARED | FLAG_LOCKED);
	}

	uint32_t Counter() const
	{
		return static_cast<uint32_t>(Value & (FLAG_WAKING | FLAG_SPINNING | FLAG_LOCKED));
	}

	bool IsCounterFull() const
	{
		return Counter() == (FLAG_WAKING | FLAG_SPINNING | FLAG_LOCKED);
	}

	size_t WithFullCounter() const
	{
		return Value | (FLAG_WAKING | FLAG_SPINNING | FLAG_LOCKED);
	}

	void ReplaceFlagPart(size_t flagPart)
	{
		Value = (Value & ~FLAG_ALL) | (flagPart & FLAG_ALL);
	}
};

//////////////////////////////////////////////////////////////////////////
void Backoff(uint32_t *pCount);
void Spinning(SRWStackNode &stackNode);

//////////////////////////////////////////////////////////////////////////
// 查找通知节点
static SRWStackNode* FindNotifyNode(SRWStackNode *pWaitNode)
{
	SRWStackNode *pNotify = pWaitNode->Notify;
	if (pNotify)
		return pNotify;

	SRWStackNode *pCurr = pWaitNode;
	do
	{
		SRWStackNode *pLast = pCurr;
		pCurr = pCurr->Back;
		pCurr->Next = pLast;
		pNotify = pCurr->Notify;
	} while (!pNotify);

	return pNotify;
}

// 查找通知节点并连接
static SRWStackNode* UpdateNotifyNode(SRWStackNode *pWaitNode)
{
	SRWStackNode *pNotify = FindNotifyNode(pWaitNode);
	pWaitNode->Notify = pNotify;
	return pNotify;
}

// 尝试清除唤醒状态
static bool TryClearWaking(size_t *pLockStatus, SRWStatus &lastStatus)
{
	SRWStatus newStatus = lastStatus.Value - FLAG_WAKING;
	AssertDebug(!newStatus.Waking);
	AssertDebug(newStatus.Locked);

	newStatus = Atomic::CompareExchange<size_t>(pLockStatus, lastStatus.Value, newStatus.Value);
	if (newStatus == lastStatus)
		return true;

	lastStatus = newStatus;
	return false;
}

PLATFORM_NOINLINE static void WakeUpLock(size_t *pLockStatus, SRWStatus lastStatus, bool isForce = false)
{
	SRWStackNode *pNotify;
	for (;;)
	{
		AssertDebug(!lastStatus.MultiShared);

		if (!isForce)
		{
			// 锁定状态尝试清除唤醒标记
			while (lastStatus.Locked)
			{
				AssertDebug(lastStatus.Spinning);
				if (TryClearWaking(pLockStatus, lastStatus))
					return;
			}
		}

		// 寻找需要通知的节点
		SRWStackNode *pWaitNode = lastStatus.WaitNode();
		pNotify = UpdateNotifyNode(pWaitNode);

		if (pNotify->Flags & FLAG_LOCKED)
		{
			if (isForce)
			{
				Atomic::FetchAnd<size_t>(pLockStatus, ~FLAG_WAKING);
				return;
			}

			if (SRWStackNode *pNext = pNotify->Next)
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
		}

		// 更新状态
		SRWStatus currStatus = Atomic::CompareExchange<size_t>(
			pLockStatus,
			lastStatus.Value,
			isForce
				? (FLAG_SHARED | FLAG_LOCKED)
				: 0);
		if (currStatus == lastStatus)
			break;

		lastStatus = currStatus;
	}

	// 唤醒需要通知的节点
	do
	{
		// 正向遍历通知节点链表
		SRWStackNode *pNext = pNotify->Next;

		Atomic::FetchBitSet(&pNotify->Flags, BIT_WAKING);

		// 尝试清除自旋标记
		if (!Atomic::FetchBitClear(&pNotify->Flags, BIT_SPINNING))
		{
			// 如果之前不在自旋则唤醒
			pNotify->WakeUp();
		}

		pNotify = pNext;
	} while (pNotify);
}

static void OptimizeLockList(size_t *pLockStatus, SRWStatus lastStatus)
{
	// 锁定状态时循环
	while (lastStatus.Locked)
	{
		// 更新通知节点
		SRWStackNode *pWaitNode = lastStatus.WaitNode();
		UpdateNotifyNode(pWaitNode);

		// 尝试清除唤醒标记
		if (TryClearWaking(pLockStatus, lastStatus))
			return;
	}
	// 如果是无锁定状态则唤醒
	WakeUpLock(pLockStatus, lastStatus);
}

template <bool IsExclusive>
static bool QueueStackNode(size_t *pLockStatus, SRWStackNode *pStackNode, SRWStatus lastStatus)
{
	SRWStatus newStatus;
	bool isOptimize = false;
	pStackNode->Next = nullptr;

	if (lastStatus.Spinning)
	{
		// 其他线程正在自旋
		pStackNode->SharedCount = -1;
		pStackNode->Notify = nullptr;
		// 作为当前线程的上一个节点挂接
		pStackNode->Back = lastStatus.WaitNode();
		// 继承多重共享标记, 并设置唤醒, 自旋和锁定和标记
		newStatus = reinterpret_cast<size_t>(pStackNode) | (lastStatus.Value & FLAG_MULTI_SHARED) | FLAG_WAKING | FLAG_SPINNING | FLAG_LOCKED;

		// 不包含唤醒标记的情况需要尝试优化链表
		if (!lastStatus.Waking)
			isOptimize = true;
	}
	else
	{
		// 把当前线程作为下一个通知节点
		pStackNode->Notify = pStackNode;
		newStatus = reinterpret_cast<size_t>(pStackNode) | FLAG_SPINNING | FLAG_LOCKED;

		if (IsExclusive)
		{
			pStackNode->SharedCount = lastStatus.SharedCount;
			// 共享个数大于 1 的情况需要设置多重共享标记
			if (pStackNode->SharedCount > 1)
				newStatus.MultiShared = 1;
			else if (!pStackNode->SharedCount)
				pStackNode->SharedCount = -2;
		}
		else
		{
			pStackNode->SharedCount = -2;
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
		return true;
	}
	return false;
}
