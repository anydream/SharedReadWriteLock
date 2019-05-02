#include "SRWLock.hpp"
#include "Utility.hpp"
#include "DebugLog.hpp"
#include <thread>
#include <mutex>
#include <shared_mutex>

//////////////////////////////////////////////////////////////////////////
PLATFORM_NOINLINE static void SimpleTest()
{
	SRWLock lk;

	{
		Assert(lk.try_lock());
		lk.unlock();
		Assert(lk.try_lock());
		lk.unlock();

		Assert(lk.try_lock_shared());
		Assert(lk.try_lock_shared());
		lk.unlock_shared();
		lk.unlock_shared();

		lk.lock();
		Assert(!lk.try_lock());
		Assert(!lk.try_lock_shared());
		lk.unlock();

		lk.lock();
		lk.unlock();

		Assert(lk.try_lock_shared());
		lk.unlock_shared();

		lk.lock_shared();
		Assert(!lk.try_lock());
		lk.unlock_shared();

		lk.lock_shared();
		lk.unlock_shared();

		lk.lock_shared();
		lk.lock_shared();
		lk.lock_shared();
		lk.unlock_shared();
		lk.unlock_shared();

		Assert(!lk.try_lock());
		lk.unlock_shared();

		Assert(lk.try_lock());
		lk.unlock();

		lk.lock();
		lk.unlock();

		puts("Basic OK");
	}

	{
		bool locked = true;
		uint32_t stt = 0;
		std::thread thd([&lk, &locked, &stt]()
		{
			stt = clock();
			lk.lock();
			std::this_thread::sleep_for(std::chrono::milliseconds(500));
			locked = false;
			lk.unlock();
		});
		std::this_thread::sleep_for(std::chrono::milliseconds(50));

		lk.lock();
		Assert(locked == false);
		lk.unlock();

		printf("Delay: %ums\n",
		       clock() - stt);

		thd.join();

		puts("Exclusive Wake OK");
	}

	{
		bool locked = true;
		uint32_t stt = 0;
		std::thread thd([&lk, &locked, &stt]()
		{
			stt = clock();
			lk.lock();
			std::this_thread::sleep_for(std::chrono::milliseconds(500));
			locked = false;
			lk.unlock();
		});
		std::this_thread::sleep_for(std::chrono::milliseconds(50));

		lk.lock_shared();
		Assert(locked == false);
		lk.unlock_shared();

		printf("Delay: %ums\n",
		       clock() - stt);

		thd.join();

		puts("Blocked Shared Wake OK");
	}

	{
		bool locked = true;
		uint32_t stt = 0;
		std::thread thd([&lk, &locked, &stt]()
		{
			stt = clock();
			lk.lock_shared();
			std::this_thread::sleep_for(std::chrono::milliseconds(500));
			locked = false;
			lk.unlock_shared();
		});
		std::this_thread::sleep_for(std::chrono::milliseconds(50));

		lk.lock();
		Assert(locked == false);
		lk.unlock();

		printf("Delay: %ums\n",
		       clock() - stt);

		thd.join();

		puts("Blocked Exclusive Wake OK");
	}

	{
		SRWLock syncLock;
		syncLock.lock();

		volatile uint32_t count = 0;
		auto func = [&lk, &syncLock, &count](uint32_t *num)
		{
			syncLock.lock_shared();

			bool loop = true;
			do
			{
				lk.lock();
				if (count < 50000000)
				{
					++count;
					++*num;
				}
				else
					loop = false;
				lk.unlock();
			} while (loop);
		};

		uint32_t c1 = 0, c2 = 0;
		std::thread thd1(std::bind(func, &c1));
		std::thread thd2(std::bind(func, &c2));
		std::this_thread::sleep_for(std::chrono::milliseconds(50));

		uint32_t stt = clock();
		syncLock.unlock();

		thd1.join();
		thd2.join();

		printf("Delay: %ums\n",
		       clock() - stt);

		printf("2 Thread Exclusive: %d (%d, %d)\n",
		       count, c1, c2);
	}

	{
		SRWLock syncLock;
		syncLock.lock();

		volatile uint32_t count = 0;
		auto func = [&lk, &syncLock, &count](uint32_t *num)
		{
			syncLock.lock_shared();

			bool loop = true;
			do
			{
				lk.lock_shared();
				if (count < 50000000)
				{
					++count;
					++*num;
				}
				else
					loop = false;
				lk.unlock_shared();
			} while (loop);
		};

		uint32_t c1 = 0, c2 = 0;
		std::thread thd1(std::bind(func, &c1));
		std::thread thd2(std::bind(func, &c2));
		std::this_thread::sleep_for(std::chrono::milliseconds(50));

		uint32_t stt = clock();
		syncLock.unlock();

		thd1.join();
		thd2.join();

		printf("Delay: %ums\n",
		       clock() - stt);

		printf("2 Thread Shared: %d (%d, %d)\n",
		       count, c1, c2);
	}
}

//////////////////////////////////////////////////////////////////////////
template <class TLock>
static void TestLockRace(const char *name, uint32_t loops)
{
	SRWLock syncLock;

	TLock locker;
	uint32_t sum = 0;

	auto func = [&locker, &sum, loops, &syncLock]()
	{
		syncLock.lock();
		syncLock.unlock();

		for (uint32_t i = 0; i < loops; ++i)
		{
			std::lock_guard<TLock> lk(locker);
			++sum;
		}
	};

	syncLock.lock();

	std::thread thd1(func);
	std::thread thd2(func);

	auto t = GetTickMicrosec();
	syncLock.unlock();

	thd1.join();
	thd2.join();

	t = GetTickMicrosec() - t;
	printf("[Race]   %s: %u, %gms\n",
	       name, sum, t / 1000.0);
	Assert(sum == loops * 2);
}

template <class TLock>
static void TestLockSingle(const char *name, uint32_t loops)
{
	TLock locker;
	uint32_t sum = 0;

	auto t = GetTickMicrosec();

	for (uint32_t i = 0; i < loops; ++i)
	{
		std::lock_guard<TLock> lk(locker);
		++sum;
	}

	t = GetTickMicrosec() - t;
	printf("[Single] %s: %u, %gms\n",
	       name, sum, t / 1000.0);
	Assert(sum == loops);
}

PLATFORM_NOINLINE static void TestLockPerf()
{
	const uint32_t loops =
#if defined(PLATFORM_IS_DEBUG) || defined(PLATFORM_IS_IPHONE)
		1000000;
#else
		10000000;
#endif

	TestLockRace<SRWLock>("SRWLock", loops);
#if !defined(PLATFORM_IS_APPLE)
	TestLockRace<std::mutex>("std::mutex", loops);
#endif
#if !defined(PLATFORM_IS_IPHONE)
	TestLockRace<std::shared_mutex>("std::shared_mutex", loops);
#endif

	TestLockSingle<SRWLock>("SRWLock", loops);
#if !defined(PLATFORM_IS_APPLE)
	TestLockSingle<std::mutex>("std::mutex", loops);
#endif
#if !defined(PLATFORM_IS_IPHONE)
	TestLockSingle<std::shared_mutex>("std::shared_mutex", loops);
#endif
}

//////////////////////////////////////////////////////////////////////////
template <class TLock>
static uint64_t TestLockSwitch(uint32_t waitTime = 250)
{
	TLock locker;
	uint64_t unlockTime = 0;
	volatile bool isLocked = false;

	std::thread thd([&locker, &unlockTime, &isLocked]()
	{
		locker.lock();
		isLocked = true;
		std::this_thread::sleep_for(std::chrono::milliseconds(500));

		unlockTime = GetTickMicrosec();
		locker.unlock();
	});

	while (!isLocked)
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	std::this_thread::sleep_for(std::chrono::milliseconds(waitTime));

	locker.lock();
	uint64_t switchTime = GetTickMicrosec() - unlockTime;
	locker.unlock();

	thd.join();

	return switchTime;
}

template <class TLock>
PLATFORM_NOINLINE static void TestLockSwitchSeq(const char *name)
{
	uint64_t result = TestLockSwitch<TLock>(250);
	printf("[Switch] %s 1: %llu microsec\n", name, result);
	result = TestLockSwitch<TLock>(480);
	printf("[Switch] %s 2: %llu microsec\n", name, result);
	result = TestLockSwitch<TLock>(495);
	printf("[Switch] %s 3: %llu microsec\n", name, result);
}

PLATFORM_NOINLINE static void TestLockSwitchPerf()
{
	TestLockSwitchSeq<SRWLock>("SRWLock");
	TestLockSwitchSeq<std::mutex>("std::mutex");

#if !defined(PLATFORM_IS_IPHONE)
	TestLockSwitchSeq<std::shared_mutex>("std::shared_mutex");
#endif
}

//////////////////////////////////////////////////////////////////////////
int main()
{
	TestLockSwitchPerf();
	TestLockPerf();
	return 0;
}
