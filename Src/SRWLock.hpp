#pragma once

#include "Predefines.hpp"

//////////////////////////////////////////////////////////////////////////
bool SRWLock_TryLock(size_t *pLockStatus);
void SRWLock_Lock(size_t *pLockStatus);
void SRWLock_Unlock(size_t *pLockStatus);
bool SRWLock_TryLockShared(size_t *pLockStatus);
void SRWLock_LockShared(size_t *pLockStatus);
void SRWLock_UnlockShared(size_t *pLockStatus);

//////////////////////////////////////////////////////////////////////////
class SRWLock
{
public:
	SRWLock() = default;
	SRWLock(const SRWLock &) = delete;
	SRWLock(SRWLock &&) = delete;

	bool try_lock();
	void lock();
	void unlock();

	bool try_lock_shared();
	void lock_shared();
	void unlock_shared();

	size_t* native_handle()
	{
		return &LockStatus_;
	}

private:
	size_t LockStatus_ = 0;
};

//////////////////////////////////////////////////////////////////////////
template <class T>
class LockGuard
{
public:
	explicit LockGuard(T &lk, bool isShared = false)
		: Lock_(lk)
		, IsShared_(isShared)
	{
		if (isShared)
			Lock_.lock_shared();
		else
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

		if (IsShared_)
			Lock_.unlock_shared();
		else
			Lock_.unlock();
	}

	T* mutex() const
	{
		return &Lock_;
	}

private:
	T &Lock_;
	bool IsShared_ = false;
	bool IsUnlocked_ = false;
};
