#pragma once

#include "SRWLock.hpp"
#include "LockUtils.hpp"

//////////////////////////////////////////////////////////////////////////
bool SRWCondVar_Wait(size_t *pCondStatus, size_t *pLockStatus, uint64_t timeOut, bool isShared);
void SRWCondVar_NotifyOne(size_t *pCondStatus);
void SRWCondVar_NotifyAll(size_t *pCondStatus);

//////////////////////////////////////////////////////////////////////////
class SRWCondVar
{
public:
	SRWCondVar() = default;
	SRWCondVar(const SRWCondVar &) = delete;
	SRWCondVar(SRWCondVar &&) = delete;

	// 唤醒. 如果没有加锁则可能无法唤醒
	void notify_one();
	void notify_all();

	// 等待唤醒. 微秒超时
	bool wait_for(LockGuard<SRWLock> &lock, uint64_t timeOut);
	bool wait_for(SharedLockGuard<SRWLock> &lock, uint64_t timeOut);

	void wait(LockGuard<SRWLock> &lock)
	{
		wait_for(lock, -1);
	}

	void wait(SharedLockGuard<SRWLock> &lock)
	{
		wait_for(lock, -1);
	}

	template <class Pred>
	void wait(LockGuard<SRWLock> &lock, Pred pred)
	{
		while (!pred())
			wait_for(lock, -1);
	}

	template <class Pred>
	void wait(SharedLockGuard<SRWLock> &lock, Pred pred)
	{
		while (!pred())
			wait_for(lock, -1);
	}

private:
	size_t CondStatus_ = 0;
};
