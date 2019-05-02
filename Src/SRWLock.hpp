#pragma once

#include "Predefines.hpp"

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

private:
	size_t LockStatus_ = 0;
};
