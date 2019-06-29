#pragma once

#include "SRWLock.hpp"

//////////////////////////////////////////////////////////////////////////
class SRWCondVar
{
public:
	SRWCondVar() = default;
	SRWCondVar(const SRWCondVar &) = delete;
	SRWCondVar(SRWCondVar &&) = delete;

	void notify_one();
	void notify_all();

	void wait_for(LockGuard<SRWLock> &lock, uint64_t timeOut, bool isShared = false);

	void wait(LockGuard<SRWLock> &lock, bool isShared = false)
	{
		wait_for(lock, -1, isShared);
	}

	template <class Pred>
	void wait(LockGuard<SRWLock> &lock, Pred pred, bool isShared = false)
	{
		while (!pred())
			wait(lock);
	}

private:
	size_t CondStatus_ = 0;
};
