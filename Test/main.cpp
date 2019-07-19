#include "SRWLock.hpp"
#include "SRWCondVar.hpp"
#include "Utility.hpp"
#include "DebugLog.hpp"
#include <thread>
#include <mutex>
#include <vector>
#include <deque>
#include <shared_mutex>
#include <functional>

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
static uint64_t TestLockWake(uint32_t waitTime = 250)
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
	uint64_t wakeTime = GetTickMicrosec() - unlockTime;
	locker.unlock();

	thd.join();

	return wakeTime;
}

template <class TLock>
PLATFORM_NOINLINE static void TestLockWakeSeq(const char *name)
{
	uint64_t result = TestLockWake<TLock>(250);
	printf("[Wake] %s 1: %lluus\n", name, result);
	result = TestLockWake<TLock>(480);
	printf("[Wake] %s 2: %lluus\n", name, result);
	result = TestLockWake<TLock>(495);
	printf("[Wake] %s 3: %lluus\n", name, result);
}

PLATFORM_NOINLINE static void TestLockWakePerf()
{
	TestLockWakeSeq<SRWLock>("SRWLock");
	TestLockWakeSeq<std::mutex>("std::mutex");

#if !defined(PLATFORM_IS_IPHONE)
	TestLockWakeSeq<std::shared_mutex>("std::shared_mutex");
#endif
}

//////////////////////////////////////////////////////////////////////////
PLATFORM_NOINLINE static void SimpleTestCondVar()
{
	SRWCondVar condVar;
	SRWLock lock;

	{
		LockGuard<SRWLock> lk(lock);
		condVar.notify_one();
		condVar.notify_all();
	}
	{
		LockGuard<SRWLock> lk(lock);
		auto stt = GetTickMicrosec();
		bool isTimeOut = condVar.wait_for(lk, 500000);
		auto dlt = GetTickMicrosec() - stt;
		Assert(dlt > 400000);
		Assert(isTimeOut);
	}

	{
		SharedLockGuard<SRWLock> lk(lock);
		condVar.notify_one();
		condVar.notify_all();
	}
	{
		SharedLockGuard<SRWLock> lk(lock);
		auto stt = GetTickMicrosec();
		bool isTimeOut = condVar.wait_for(lk, 500000);
		auto dlt = GetTickMicrosec() - stt;
		Assert(dlt > 400000);
		Assert(isTimeOut);
	}
}

PLATFORM_NOINLINE static void TestCondVar()
{
	bool isExit = false;

	SRWCondVar condVar;
	SRWLock lock;
	uint64_t wakeTime = 0;

	std::thread thd([&]()
	{
		{
			LockGuard<SRWLock> lk(lock);

			condVar.wait(lk, [&]()
			{
				return isExit;
			});

			uint64_t wakeElapsed = GetTickMicrosec() - wakeTime;
			printf("wait Elapsed: %lluus\n", wakeElapsed);
		}
	});

	std::this_thread::sleep_for(std::chrono::milliseconds(500));

	{
		LockGuard<SRWLock> lk(lock);
		isExit = true;
		wakeTime = GetTickMicrosec();
		condVar.notify_one();
	}

	thd.join();
}

PLATFORM_NOINLINE static void TestCondVar2()
{
	SRWCondVar condVar;
	SRWLock lock;
	uint64_t wakeTime = 0;

	std::thread thd([&]()
	{
		{
			LockGuard<SRWLock> lk(lock);
			bool isTimeOut = condVar.wait_for(lk, 4000000);
			Assert(!isTimeOut);
		}
		uint64_t wakeElapsed = GetTickMicrosec() - wakeTime;
		printf("wait_for Elapsed: %lluus\n", wakeElapsed);
	});

	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	{
		LockGuard<SRWLock> lk(lock);
		wakeTime = GetTickMicrosec();

		condVar.notify_one();
	}

	thd.join();
}

struct AvgCounter
{
	double Total_ = 0;
	double Min_ = 0;
	double Max_ = 0;
	size_t Count_ = 0;

	void AddData(double d)
	{
		Total_ += d;

		if (Count_)
		{
			Min_ = (std::min)(Min_, d);
			Max_ = (std::max)(Max_, d);
		}
		else
		{
			Min_ = Max_ = d;
		}

		++Count_;
	}

	void Print() const
	{
		printf("Min:%gus, Max:%gus, Avg:%gus, Cnt:%zu",
		       Min_, Max_, Total_ / Count_, Count_);
	}
};

template <class TCondVar, class TLock, class TGuard, class TSleep>
PLATFORM_NOINLINE static void TestCondVarSwitch(const char *name, TSleep funcSleep)
{
	const uint32_t threadCount = 4;

	struct Context
	{
		TCondVar CondVar_;
		TLock Lock_;
		std::deque<uint64_t> DataList_;

		uint64_t WaitData()
		{
			TGuard lk(Lock_);
			CondVar_.wait(lk, [this]() { return !DataList_.empty(); });
			uint64_t ret = DataList_.front();
			DataList_.pop_front();
			return ret;
		}

		void NotifyData(uint64_t d, bool isAll)
		{
			TGuard lk(Lock_);
			DataList_.push_back(d);

			if (isAll)
				CondVar_.notify_all();
			else
				CondVar_.notify_one();
		}

		void NotifyDataList(const std::vector<uint64_t> &dlst, bool isAll)
		{
			TGuard lk(Lock_);
			DataList_.insert(DataList_.end(), dlst.begin(), dlst.end());

			if (isAll)
				CondVar_.notify_all();
			else
				CondVar_.notify_one();
		}
	};

	bool isExit = false;

	Context ctxProducer, ctxCustomer;
	auto funcCustomer = [&]()
	{
		while (!isExit)
		{
			uint64_t data = ctxCustomer.WaitData();
			if (data)
				ctxProducer.NotifyData(data, false);
			else
				Assert(isExit);
		}
	};

	AvgCounter ac;
	auto funcProducer = [&]()
	{
		while (!isExit)
		{
			funcSleep();

			uint64_t ts = GetTickMicrosec();
			ctxCustomer.NotifyData(ts, false);

			uint64_t reply = ctxProducer.WaitData();
			if (reply)
			{
				AssertDo(reply == ts, printf("reply: %llu, ts: %llu\n", reply, ts));
				ac.AddData(static_cast<double>(GetTickMicrosec() - ts));
			}
			else
			{
				Assert(isExit);
			}
		}
	};

	std::vector<std::thread> thdList(threadCount);
	for (auto &thd : thdList)
		thd = std::thread(funcCustomer);

	std::thread thdProducer(funcProducer);

	std::this_thread::sleep_for(std::chrono::milliseconds(2000));

	isExit = true;
	ctxProducer.NotifyData(0, true);

	std::vector<uint64_t> tsList(threadCount);
	ctxCustomer.NotifyDataList(tsList, true);

	for (auto &thd : thdList)
		thd.join();
	thdProducer.join();

	printf("[CondVarSwitch] %s: ", name);
	ac.Print();
	puts("");
}

template <class TCondVar, class TLock, class TGuard, class TSleep>
PLATFORM_NOINLINE static void TestThunderingHerd(const char *name, TSleep funcSleep)
{
	const uint32_t threadCount = 4;

	struct Context
	{
		TCondVar CondVar_;
		TLock Lock_;
		std::deque<uint64_t> DataList_;

		uint64_t WaitData()
		{
			TGuard lk(Lock_);
			CondVar_.wait(lk, [this]() { return !DataList_.empty(); });
			uint64_t ret = DataList_.front();
			DataList_.pop_front();
			return ret;
		}

		void NotifyData(uint64_t d, bool isAll)
		{
			TGuard lk(Lock_);
			DataList_.push_back(d);

			if (isAll)
				CondVar_.notify_all();
			else
				CondVar_.notify_one();
		}

		void NotifyDataList(const std::vector<uint64_t> &dlst, bool isAll)
		{
			TGuard lk(Lock_);
			DataList_.insert(DataList_.end(), dlst.begin(), dlst.end());

			if (isAll)
				CondVar_.notify_all();
			else
				CondVar_.notify_one();
		}
	};

	bool isExit = false;

	Context ctxProducer, ctxCustomer;
	auto funcCustomer = [&]()
	{
		while (!isExit)
		{
			uint64_t data = ctxCustomer.WaitData();
			if (data)
				ctxProducer.NotifyData(data, false);
			else
				Assert(isExit);
		}
	};

	AvgCounter ac;
	auto funcProducer = [&]()
	{
		while (!isExit)
		{
			funcSleep();

			uint64_t ts = GetTickMicrosec();

			std::vector<uint64_t> tsList(threadCount);
			for (auto &i : tsList)
				i = ts;
			ctxCustomer.NotifyDataList(tsList, true);

			for (size_t i = 0; i < threadCount; ++i)
			{
				uint64_t reply = ctxProducer.WaitData();
				if (reply)
				{
					AssertDo(reply == ts, printf("reply: %llu, ts: %llu\n", reply, ts));
				}
				else
				{
					Assert(isExit);
					return;
				}
			}

			ac.AddData(static_cast<double>(GetTickMicrosec() - ts));
		}
	};

	std::vector<std::thread> thdList(threadCount);
	for (auto &thd : thdList)
		thd = std::thread(funcCustomer);

	std::thread thdProducer(funcProducer);

	std::this_thread::sleep_for(std::chrono::milliseconds(2000));

	isExit = true;
	ctxProducer.NotifyData(0, true);

	std::vector<uint64_t> tsList(threadCount);
	ctxCustomer.NotifyDataList(tsList, true);

	for (auto &thd : thdList)
		thd.join();
	thdProducer.join();

	printf("[ThunderingHerd] %s: ", name);
	ac.Print();
	puts("");
}

//////////////////////////////////////////////////////////////////////////
PLATFORM_NOINLINE static void TestSRWRecLock()
{
	SRWRecLock rl;

	rl.lock();
	rl.lock();
	Assert(rl.try_lock());
	Assert(rl.try_lock());

	rl.unlock();
	rl.unlock();
	rl.unlock();
	rl.unlock();

	{
		Assert(rl.try_lock());

		bool result = false;
		std::thread thd([&rl, &result]()
		{
			rl.lock();
			rl.unlock();
			result = true;
		});

		std::this_thread::sleep_for(std::chrono::milliseconds(500));
		Assert(!result);
		rl.unlock();

		thd.join();
		Assert(result);
	}
	{
		Assert(rl.try_lock());

		std::thread thd([&rl]()
		{
			Assert(!rl.try_lock());
		});

		thd.join();

		rl.unlock();
	}

	puts("TestSRWRecLock OK");
}

//////////////////////////////////////////////////////////////////////////
int main()
{
	uint32_t thds = std::thread::hardware_concurrency();
	printf("ProcessorThreads: %u\n", thds);

	TestSRWRecLock();

	TestCondVarSwitch<std::condition_variable, std::mutex, std::unique_lock<std::mutex>>("std::cond_var", []()
	{
	});
	TestCondVarSwitch<SRWCondVar, SRWLock, LockGuard<SRWLock>>("SRWCondVar", []()
	{
	});
	TestCondVarSwitch<std::condition_variable, std::mutex, std::unique_lock<std::mutex>>("std::cond_var.sleep(0)", []()
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(0));
	});
	TestCondVarSwitch<SRWCondVar, SRWLock, LockGuard<SRWLock>>("SRWCondVar.sleep(0)", []()
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(0));
	});
	TestCondVarSwitch<SRWCondVar, SRWLock, LockGuard<SRWLock>>("SRWCondVar.sleep(10)", []()
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	});
	TestCondVarSwitch<std::condition_variable, std::mutex, std::unique_lock<std::mutex>>("std::cond_var.sleep(10)", []()
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	});

	TestThunderingHerd<SRWCondVar, SRWLock, LockGuard<SRWLock>>("SRWCondVar", []()
	{
	});
	TestThunderingHerd<std::condition_variable, std::mutex, std::unique_lock<std::mutex>>("std::cond_var", []()
	{
	});
	TestThunderingHerd<SRWCondVar, SRWLock, LockGuard<SRWLock>>("SRWCondVar.sleep(10)", []()
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	});
	TestThunderingHerd<std::condition_variable, std::mutex, std::unique_lock<std::mutex>>("std::cond_var.sleep(10)", []()
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	});

	TestCondVar2();
	TestCondVar();
	SimpleTestCondVar();
	TestLockPerf();
	TestLockWakePerf();
	SimpleTest();
	return 0;
}
