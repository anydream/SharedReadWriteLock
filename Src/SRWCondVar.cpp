#include "SRWCondVar.hpp"
#include "SRWInternals.hpp"

//////////////////////////////////////////////////////////////////////////
struct CVStackNode : SRWStackNode
{
	// 条件变量等待的上一个锁
	size_t *LastLock;
};

//////////////////////////////////////////////////////////////////////////
PLATFORM_NOINLINE static bool QueueStackNodeToSRWLock(SRWStackNode *pStackNode, size_t *pLockStatus)
{
	SRWStatus lastStatus = *pLockStatus;
	uint32_t backoffCount = 0;

	while (lastStatus.Locked &&
		((pStackNode->Flags & FLAG_LOCKED) || lastStatus.Spinning || !lastStatus.WaitNode()))
	{
		if (QueueStackNode<true>(pLockStatus, pStackNode, lastStatus))
			return true;

		// 存在竞争时主动避让
		Backoff(&backoffCount);
		lastStatus = *pLockStatus;
	}

	return false;
}

PLATFORM_NOINLINE static void DoWakeCondVariable(size_t *pCondStatus, SRWStatus lastStatus, uint32_t addCounter)
{
	SRWStatus oldStatus;

	SRWStackNode *pCurrNotify = nullptr;
	SRWStackNode **ppCurrNotify = &pCurrNotify;
	uint32_t counter = 0;

	for (;;)
	{
		SRWStackNode *pWaitNode = lastStatus.WaitNode();

		if (lastStatus.IsCounterFull())
		{
			SRWStatus condValue = Atomic::Exchange<size_t>(pCondStatus, 0);
			*ppCurrNotify = condValue.WaitNode();
			break;
		}

		uint32_t total = addCounter + lastStatus.Counter();
		SRWStackNode *pNotify = FindNotifyNode(pWaitNode);

		while (total > counter)
		{
			SRWStackNode *pNext = pNotify->Next;
			if (!pNext)
				break;

			++counter;
			*ppCurrNotify = pNotify;
			pNotify->Back = nullptr;
			ppCurrNotify = &pNotify->Back;
			pWaitNode->Notify = pNext;
			pNotify = pNext;
			pNext->Back = nullptr;
		}

		if (total <= counter)
		{
			oldStatus = lastStatus;
			lastStatus = Atomic::CompareExchange<size_t>(pCondStatus, lastStatus.Value, reinterpret_cast<size_t>(pWaitNode));
			if (lastStatus == oldStatus)
				break;
		}
		else
		{
			oldStatus = lastStatus;
			lastStatus = Atomic::CompareExchange<size_t>(pCondStatus, lastStatus.Value, 0);
			if (lastStatus == oldStatus)
			{
				*ppCurrNotify = pNotify;
				pNotify->Back = nullptr;
				break;
			}
		}
	}

	while (pCurrNotify)
	{
		SRWStackNode *pBack = pCurrNotify->Back;
		if (!Atomic::FetchBitClear(&pCurrNotify->Flags, BIT_SPINNING))
		{
			size_t *pLastLock = static_cast<CVStackNode*>(pCurrNotify)->LastLock;
			if (!pLastLock ||
				!QueueStackNodeToSRWLock(pCurrNotify, pLastLock))
			{
				Atomic::FetchBitSet(&pCurrNotify->Flags, BIT_WAKING);
				pCurrNotify->WakeUp();
			}
		}
		pCurrNotify = pBack;
	}
}

PLATFORM_NOINLINE static void OptimizeWaitList(size_t *pCondStatus, SRWStatus lastStatus)
{
	for (;;)
	{
		SRWStackNode *pWaitNode = lastStatus.WaitNode();
		UpdateNotifyNode(pWaitNode);

		SRWStatus oldStatus = lastStatus;
		lastStatus = Atomic::CompareExchange<size_t>(pCondStatus, lastStatus.Value, reinterpret_cast<size_t>(pWaitNode));
		if (lastStatus == oldStatus)
			return;

		if (lastStatus.Counter())
		{
			DoWakeCondVariable(pCondStatus, lastStatus, 0);
			return;
		}
	}
}

PLATFORM_NOINLINE static bool WakeSingle(size_t *pCondStatus, SRWStackNode *pWaitNode)
{
	SRWStatus lastStatus = *pCondStatus;
	SRWStatus newStatus;

	for (;;)
	{
		if (!lastStatus.Value || lastStatus.IsCounterFull())
			return false;

		if (lastStatus.MultiShared)
		{
			SRWStatus oldStatus = lastStatus;
			lastStatus = Atomic::CompareExchange<size_t>(pCondStatus, lastStatus.Value, lastStatus.WithFullCounter());
			if (lastStatus == oldStatus)
				return false;
		}
		else
		{
			newStatus = lastStatus;
			newStatus.MultiShared = 1;

			SRWStatus oldStatus = lastStatus;
			lastStatus = Atomic::CompareExchange<size_t>(pCondStatus, lastStatus.Value, newStatus.Value);
			if (lastStatus == oldStatus)
				break;
		}
	}

	lastStatus = newStatus;

	SRWStackNode *pCurr = newStatus.WaitNode();
	SRWStackNode *pLastWait = pCurr;
	SRWStackNode *pLast = nullptr;
	bool result = false;

	if (pCurr)
	{
		do
		{
			SRWStackNode *pBack = pCurr->Back;

			if (pCurr != pWaitNode)
			{
				pCurr->Next = pLast;
				pLast = pCurr;
				pCurr = pBack;
				continue;
			}

			if (pLast)
			{
				Atomic::FetchBitSet(&pCurr->Flags, BIT_WAKING);
				result = true;

				pLast->Back = pBack;
				if (pBack)
					pBack->Next = pLast;

				pCurr = pBack;
				continue;
			}

			newStatus = reinterpret_cast<size_t>(pBack);
			if (newStatus.Value)
				newStatus.ReplaceFlagPart(lastStatus.Value);

			SRWStatus oldStatus = lastStatus;
			lastStatus = Atomic::CompareExchange<size_t>(pCondStatus, lastStatus.Value, newStatus.Value);
			if (lastStatus == oldStatus)
			{
				Atomic::FetchBitSet(&pCurr->Flags, BIT_WAKING);
				result = true;

				lastStatus = newStatus;
				if (!pBack)
					return true;
			}
			else
				newStatus = lastStatus;

			pCurr = lastStatus.WaitNode();
			pLastWait = pCurr;
			pLast = nullptr;
		} while (pCurr);

		if (pLastWait)
			pLastWait->Notify = pLast;

		if (!result)
			Atomic::FetchBitSet(&pWaitNode->Flags, BIT_SPINNING);
	}
	else
		Atomic::FetchBitSet(&pWaitNode->Flags, BIT_SPINNING);

	DoWakeCondVariable(pCondStatus, newStatus, 0);

	if (!result)
		result = !Atomic::FetchBitClear(&pWaitNode->Flags, BIT_SPINNING);

	return result;
}

PLATFORM_NOINLINE bool SRWCondVar_Wait(size_t *pCondStatus, size_t *pLockStatus, uint64_t timeOut, bool isShared)
{
	SRWStatus newStatus;
	alignas(16) CVStackNode stackNode{};

	SRWStatus lastStatus = *pCondStatus;
	stackNode.Next = nullptr;
	stackNode.LastLock = pLockStatus;

	if (isShared)
		stackNode.Flags = FLAG_SPINNING;
	else
		stackNode.Flags = FLAG_SPINNING | FLAG_LOCKED;

	for (;;)
	{
		newStatus = reinterpret_cast<size_t>(&stackNode) | (lastStatus.Value & FLAG_ALL);
		stackNode.Back = lastStatus.WaitNode();
		if (stackNode.Back)
		{
			stackNode.Notify = nullptr;
			newStatus.MultiShared = 1;
		}
		else
		{
			stackNode.Notify = &stackNode;
		}

		SRWStatus oldStatus = lastStatus;
		lastStatus = Atomic::CompareExchange<size_t>(pCondStatus, lastStatus.Value, newStatus.Value);
		if (lastStatus == oldStatus)
			break;
	}

	if (isShared)
		SRWLock_UnlockShared(pLockStatus);
	else
		SRWLock_Unlock(pLockStatus);

	if (lastStatus.MultiShared != newStatus.MultiShared)
		OptimizeWaitList(pCondStatus, newStatus);

	Spinning(stackNode);

	bool isTimeOut = false;
	if (Atomic::FetchBitClear(&stackNode.Flags, BIT_SPINNING))
		isTimeOut = stackNode.WaitMicrosec(timeOut);
	else
		Atomic::FetchBitSet(&stackNode.Flags, BIT_WAKING);

	if (isTimeOut || !(stackNode.Flags & FLAG_WAKING))
	{
		if (!WakeSingle(pCondStatus, &stackNode))
		{
			do
			{
				stackNode.WaitMicrosec();
			} while (!(stackNode.Flags & FLAG_WAKING));

			isTimeOut = false;
		}
	}

	if (isShared)
		SRWLock_LockShared(pLockStatus);
	else
		SRWLock_Lock(pLockStatus);

	return isTimeOut;
}

PLATFORM_NOINLINE void SRWCondVar_NotifyOne(size_t *pCondStatus)
{
	SRWStatus lastStatus = *pCondStatus;

	while (lastStatus.Value)
	{
		SRWStatus currStatus;
		if (lastStatus.MultiShared)
		{
			if (lastStatus.IsCounterFull())
				return;

			currStatus = Atomic::CompareExchange<size_t>(pCondStatus, lastStatus.Value, lastStatus.Value + 1);
			if (currStatus == lastStatus)
				return;
		}
		else
		{
			SRWStatus newStatus = lastStatus;
			newStatus.MultiShared = 1;
			currStatus = Atomic::CompareExchange<size_t>(pCondStatus, lastStatus.Value, newStatus.Value);
			if (currStatus == lastStatus)
			{
				DoWakeCondVariable(pCondStatus, newStatus, 1);
				return;
			}
		}

		lastStatus = currStatus;
	}
}

PLATFORM_NOINLINE void SRWCondVar_NotifyAll(size_t *pCondStatus)
{
	SRWStatus lastStatus = *pCondStatus;

	while (lastStatus.Value && !lastStatus.IsCounterFull())
	{
		SRWStatus currStatus;
		if (lastStatus.MultiShared)
		{
			currStatus = Atomic::CompareExchange<size_t>(pCondStatus, lastStatus.Value, lastStatus.WithFullCounter());
			if (currStatus == lastStatus)
				return;
		}
		else
		{
			currStatus = Atomic::CompareExchange<size_t>(pCondStatus, lastStatus.Value, 0);
			if (currStatus == lastStatus)
			{
				SRWStackNode *pWaitNode = lastStatus.WaitNode();
				while (pWaitNode)
				{
					SRWStackNode *pBack = pWaitNode->Back;

					Atomic::FetchBitSet(&pWaitNode->Flags, BIT_WAKING);

					if (!Atomic::FetchBitClear(&pWaitNode->Flags, BIT_SPINNING))
						pWaitNode->WakeUp();

					pWaitNode = pBack;
				}
				return;
			}
		}

		lastStatus = currStatus;
	}
}

//////////////////////////////////////////////////////////////////////////
void SRWCondVar::notify_one()
{
	SRWCondVar_NotifyOne(&CondStatus_);
}

void SRWCondVar::notify_all()
{
	SRWCondVar_NotifyAll(&CondStatus_);
}

bool SRWCondVar::wait_for(LockGuard<SRWLock> &lock, uint64_t timeOut)
{
	return SRWCondVar_Wait(&CondStatus_, lock.mutex()->native_handle(), timeOut, false);
}

bool SRWCondVar::wait_for(SharedLockGuard<SRWLock> &lock, uint64_t timeOut)
{
	return SRWCondVar_Wait(&CondStatus_, lock.mutex()->native_handle(), timeOut, true);
}
