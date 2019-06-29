#include "SRWCondVar.hpp"
#include "SRWInternals.hpp"

//////////////////////////////////////////////////////////////////////////
// PASS
static bool QueueStackNodeToSRWLock(SRWStackNode *pStackNode, SRWLock *pSRWLock)
{
	SRWStatus lastStatus = *pSRWLock->native_handle();
	uint32_t backoffCount = 0;

	while (lastStatus.Locked &&
		((pStackNode->Flags & FLAG_LOCKED) || lastStatus.Spinning || !lastStatus.SharedCount))
	{
		if (QueueStackNode<true>(pSRWLock->native_handle(), pStackNode, lastStatus))
			return true;

		// 存在竞争时主动避让
		Backoff(&backoffCount);
		lastStatus = *pSRWLock->native_handle();
	}

	return false;
}

// PASS
static void DoWakeCondVariable(size_t *pCondStatus, SRWStatus lastStatus, uint32_t addCounter)
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
			pNext->Back = nullptr;
			pNotify = pNext;
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
			if (!pCurrNotify->LastLock ||
				!QueueStackNodeToSRWLock(pCurrNotify, pCurrNotify->LastLock))
			{
				Atomic::FetchBitSet(&pCurrNotify->Flags, BIT_WAKING);
				pCurrNotify->WakeUp();
			}
		}
		pCurrNotify = pBack;
	}
}

// PASS
static void OptimizeWaitList(size_t *pCondStatus, SRWStatus lastStatus)
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

// PASS
static bool WakeSingle(size_t *pCondStatus, SRWStackNode *pWaitNode)
{
	SRWStatus newStatus;
	SRWStatus lastStatus = *pCondStatus;

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
			SRWStatus oldStatus = lastStatus;
			newStatus = lastStatus;
			newStatus.MultiShared = 1;
			lastStatus = Atomic::CompareExchange<size_t>(pCondStatus, lastStatus.Value, newStatus.Value);

			if (lastStatus == oldStatus)
				break;
		}
	}

	lastStatus = newStatus;
	SRWStackNode *pCurr = lastStatus.WaitNode();
	SRWStackNode *pLastWait = pCurr;
	SRWStackNode *pLast = nullptr;
	bool result = false;

	if (pCurr)
	{
		do
		{
			if (pCurr == pWaitNode)
			{
				if (pLast)
				{
					// TODO
					Atomic::FetchBitSet(&pCurr->Flags, BIT_WAKING);

					result = true;
					pCurr = pCurr->Back;
					pLast->Back = pCurr;
					if (pCurr)
						pCurr->Next = pLast;
				}
				else
				{
					SRWStackNode *pBack = pCurr->Back;
					newStatus = reinterpret_cast<size_t>(pBack);
					if (newStatus.Value)
						newStatus.ReplaceFlagPart(lastStatus.Value);

					SRWStatus oldStatus = lastStatus;
					lastStatus = Atomic::CompareExchange<size_t>(pCondStatus, lastStatus.Value, newStatus.Value);
					if (lastStatus == oldStatus)
					{
						// TODO
						Atomic::FetchBitSet(&pCurr->Flags, BIT_WAKING);

						lastStatus = newStatus;
						if (!pBack)
							return true;
						result = true;
					}
					else
					{
						newStatus = lastStatus;
					}

					pCurr = lastStatus.WaitNode();
					pLastWait = pCurr;
					pLast = nullptr;
				}
			}
			else
			{
				pCurr->Next = pLast;
				pLast = pCurr;
				pCurr = pCurr->Back;
			}
		} while (pCurr);

		if (pLastWait)
			pLastWait->Notify = pLast;

		if (!result)
		{
			// TODO
			Atomic::FetchBitSet(&pWaitNode->Flags, BIT_SPINNING);
		}
	}
	else
	{
		// TODO
		Atomic::FetchBitSet(&pWaitNode->Flags, BIT_SPINNING);
	}

	DoWakeCondVariable(pCondStatus, newStatus, 0);

	// TODO
	if (!result)
	{
		if (!Atomic::FetchBitClear(&pWaitNode->Flags, BIT_SPINNING))
			return true;
	}

	return result;
}

static bool SleepCondVariable(size_t *pCondStatus, SRWLock *pSRWLock, uint64_t timeOut, bool isShared)
{
	SRWStatus newStatus;
	alignas(16) SRWStackNode stackNode;

	SRWStatus lastStatus = *pCondStatus;
	stackNode.Next = nullptr;
	stackNode.LastLock = pSRWLock;

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
		pSRWLock->unlock_shared();
	else
		pSRWLock->unlock();

	if (lastStatus.MultiShared != newStatus.MultiShared)
		OptimizeWaitList(pCondStatus, newStatus);

#pragma nounroll
	for (uint32_t spinCount = g_SRWSpinCount; spinCount; --spinCount)
	{
		if (!(static_cast<volatile const uint32_t&>(stackNode.Flags) & FLAG_SPINNING))
			break;
		PLATFORM_YIELD;
	}

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
		pSRWLock->lock_shared();
	else
		pSRWLock->lock();

	return isTimeOut;
}

// PASS
static void WakeCondVariable(size_t *pCondStatus)
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

// PASS
static void WakeAllCondVariable(size_t *pCondStatus)
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

					// TODO
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
	WakeCondVariable(&CondStatus_);
}

void SRWCondVar::notify_all()
{
	WakeAllCondVariable(&CondStatus_);
}

bool SRWCondVar::wait_for(LockGuard<SRWLock> &lock, uint64_t timeOut, bool isShared)
{
	return SleepCondVariable(&CondStatus_, lock.mutex(), timeOut, isShared);
}
