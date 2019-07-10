#pragma once

#include "Predefines.hpp"

//////////////////////////////////////////////////////////////////////////
template <class T>
class LockGuard
{
public:
	explicit LockGuard(T &lk)
		: Lock_(lk)
	{
		Lock_.lock();
	}

	~LockGuard()
	{
		unlock();
	}

	// 解锁
	void unlock()
	{
		if (IsUnlocked_)
			return;

		IsUnlocked_ = true;

		Lock_.unlock();
	}

	T* mutex() const
	{
		return &Lock_;
	}

private:
	T &Lock_;
	bool IsUnlocked_ = false;
};

//////////////////////////////////////////////////////////////////////////
template <class T>
class SharedLockGuard
{
public:
	explicit SharedLockGuard(T &lk)
		: Lock_(lk)
	{
		Lock_.lock_shared();
	}

	~SharedLockGuard()
	{
		unlock();
	}

	// 解锁
	void unlock()
	{
		if (IsUnlocked_)
			return;

		IsUnlocked_ = true;

		Lock_.unlock_shared();
	}

	T* mutex() const
	{
		return &Lock_;
	}

private:
	T &Lock_;
	bool IsUnlocked_ = false;
};
