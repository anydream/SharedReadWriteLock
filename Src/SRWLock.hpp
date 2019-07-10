#pragma once

#include "Predefines.hpp"

//////////////////////////////////////////////////////////////////////////
void SRWLock_Init();

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
