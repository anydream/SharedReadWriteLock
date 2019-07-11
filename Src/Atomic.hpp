#pragma once

#include "Predefines.hpp"

namespace Atomic
{
	template <class T,
	          ENABLE_IF(std::is_integral<T>::value && sizeof(T) == 1)>
	T CompareExchange(T *pDest, T cmpVal, T newVal)
	{
#if defined(PLATFORM_GNUC_LIKE)
		return __sync_val_compare_and_swap(pDest, cmpVal, newVal);
#elif defined(PLATFORM_IS_WINDOWS)
		return _InterlockedCompareExchange8(reinterpret_cast<volatile char*>(pDest), newVal, cmpVal);
#endif
	}

	template <class T,
	          ENABLE_IF(std::is_integral<T>::value && sizeof(T) == 2)>
	T CompareExchange(T *pDest, T cmpVal, T newVal)
	{
#if defined(PLATFORM_GNUC_LIKE)
		return __sync_val_compare_and_swap(pDest, cmpVal, newVal);
#elif defined(PLATFORM_IS_WINDOWS)
		return _InterlockedCompareExchange16(reinterpret_cast<volatile short*>(pDest), newVal, cmpVal);
#endif
	}

	template <class T,
	          ENABLE_IF(std::is_integral<T>::value && sizeof(T) == 4)>
	T CompareExchange(T *pDest, T cmpVal, T newVal)
	{
#if defined(PLATFORM_GNUC_LIKE)
		return __sync_val_compare_and_swap(pDest, cmpVal, newVal);
#elif defined(PLATFORM_IS_WINDOWS)
		return _InterlockedCompareExchange(reinterpret_cast<volatile long*>(pDest), newVal, cmpVal);
#endif
	}

	template <class T,
	          ENABLE_IF(std::is_integral<T>::value && sizeof(T) == 8)>
	T CompareExchange(T *pDest, T cmpVal, T newVal)
	{
#if defined(PLATFORM_GNUC_LIKE)
		return __sync_val_compare_and_swap(pDest, cmpVal, newVal);
#elif defined(PLATFORM_IS_WINDOWS)
		return _InterlockedCompareExchange64(reinterpret_cast<volatile long long*>(pDest), newVal, cmpVal);
#endif
	}

	//////////////////////////////////////////////////////////////////////////
	template <class T,
	          ENABLE_IF(std::is_integral<T>::value && sizeof(T) == 1)>
	T Exchange(T *pDest, T val)
	{
#if defined(PLATFORM_GNUC_LIKE)
		return __atomic_exchange_n(pDest, val, __ATOMIC_ACQ_REL);
#elif defined(PLATFORM_IS_WINDOWS)
		return _InterlockedExchange8(reinterpret_cast<volatile char*>(pDest), val);
#endif
	}

	template <class T,
	          ENABLE_IF(std::is_integral<T>::value && sizeof(T) == 2)>
	T Exchange(T *pDest, T val)
	{
#if defined(PLATFORM_GNUC_LIKE)
		return __atomic_exchange_n(pDest, val, __ATOMIC_ACQ_REL);
#elif defined(PLATFORM_IS_WINDOWS)
		return _InterlockedExchange16(reinterpret_cast<volatile short*>(pDest), val);
#endif
	}

	template <class T,
	          ENABLE_IF(std::is_integral<T>::value && sizeof(T) == 4)>
	T Exchange(T *pDest, T val)
	{
#if defined(PLATFORM_GNUC_LIKE)
		return __atomic_exchange_n(pDest, val, __ATOMIC_ACQ_REL);
#elif defined(PLATFORM_IS_WINDOWS)
		return _InterlockedExchange(reinterpret_cast<volatile long*>(pDest), val);
#endif
	}

#if defined(PLATFORM_IS_64BIT)
	template <class T,
	          ENABLE_IF(std::is_integral<T>::value && sizeof(T) == 8)>
	T Exchange(T *pDest, T val)
	{
#if defined(PLATFORM_GNUC_LIKE)
		return __atomic_exchange_n(pDest, val, __ATOMIC_ACQ_REL);
#elif defined(PLATFORM_IS_WINDOWS)
		return _InterlockedExchange64(reinterpret_cast<volatile long long*>(pDest), val);
#endif
	}
#endif

	//////////////////////////////////////////////////////////////////////////
	template <class T,
	          ENABLE_IF(std::is_integral<T>::value && sizeof(T) == 2)>
	T IncrementFetch(T *pDest)
	{
#if defined(PLATFORM_GNUC_LIKE)
		return __atomic_add_fetch(pDest, 1, __ATOMIC_ACQ_REL);
#elif defined(PLATFORM_IS_WINDOWS)
		return _InterlockedIncrement16(reinterpret_cast<volatile short*>(pDest));
#endif
	}

	template <class T,
	          ENABLE_IF(std::is_integral<T>::value && sizeof(T) == 4)>
	T IncrementFetch(T *pDest)
	{
#if defined(PLATFORM_GNUC_LIKE)
		return __atomic_add_fetch(pDest, 1, __ATOMIC_ACQ_REL);
#elif defined(PLATFORM_IS_WINDOWS)
		return _InterlockedIncrement(reinterpret_cast<volatile long*>(pDest));
#endif
	}

#if defined(PLATFORM_IS_64BIT)
	template <class T,
	          ENABLE_IF(std::is_integral<T>::value && sizeof(T) == 8)>
	T IncrementFetch(T *pDest)
	{
#if defined(PLATFORM_GNUC_LIKE)
		return __atomic_add_fetch(pDest, 1, __ATOMIC_ACQ_REL);
#elif defined(PLATFORM_IS_WINDOWS)
		return _InterlockedIncrement64(reinterpret_cast<volatile long long*>(pDest));
#endif
	}
#endif

	//////////////////////////////////////////////////////////////////////////
	template <class T,
	          ENABLE_IF(std::is_integral<T>::value && sizeof(T) == 2)>
	T DecrementFetch(T *pDest)
	{
#if defined(PLATFORM_GNUC_LIKE)
		return __atomic_sub_fetch(pDest, 1, __ATOMIC_ACQ_REL);
#elif defined(PLATFORM_IS_WINDOWS)
		return _InterlockedDecrement16(reinterpret_cast<volatile short*>(pDest));
#endif
	}

	template <class T,
	          ENABLE_IF(std::is_integral<T>::value && sizeof(T) == 4)>
	T DecrementFetch(T *pDest)
	{
#if defined(PLATFORM_GNUC_LIKE)
		return __atomic_sub_fetch(pDest, 1, __ATOMIC_ACQ_REL);
#elif defined(PLATFORM_IS_WINDOWS)
		return _InterlockedDecrement(reinterpret_cast<volatile long*>(pDest));
#endif
	}

#if defined(PLATFORM_IS_64BIT)
	template <class T,
	          ENABLE_IF(std::is_integral<T>::value && sizeof(T) == 8)>
	T DecrementFetch(T *pDest)
	{
#if defined(PLATFORM_GNUC_LIKE)
		return __atomic_sub_fetch(pDest, 1, __ATOMIC_ACQ_REL);
#elif defined(PLATFORM_IS_WINDOWS)
		return _InterlockedDecrement64(reinterpret_cast<volatile long long*>(pDest));
#endif
	}
#endif

	//////////////////////////////////////////////////////////////////////////
	template <class T,
	          ENABLE_IF(std::is_integral<T>::value && sizeof(T) == 1)>
	T FetchAdd(T *pDest, T val)
	{
#if defined(PLATFORM_GNUC_LIKE)
		return __atomic_fetch_add(pDest, val, __ATOMIC_ACQ_REL);
#elif defined(PLATFORM_IS_WINDOWS)
		return _InterlockedExchangeAdd8(reinterpret_cast<volatile char*>(pDest), val);
#endif
	}

	template <class T,
	          ENABLE_IF(std::is_integral<T>::value && sizeof(T) == 2)>
	T FetchAdd(T *pDest, T val)
	{
#if defined(PLATFORM_GNUC_LIKE)
		return __atomic_fetch_add(pDest, val, __ATOMIC_ACQ_REL);
#elif defined(PLATFORM_IS_WINDOWS)
		return _InterlockedExchangeAdd16(reinterpret_cast<volatile short*>(pDest), val);
#endif
	}

	template <class T,
	          ENABLE_IF(std::is_integral<T>::value && sizeof(T) == 4)>
	T FetchAdd(T *pDest, T val)
	{
#if defined(PLATFORM_GNUC_LIKE)
		return __atomic_fetch_add(pDest, val, __ATOMIC_ACQ_REL);
#elif defined(PLATFORM_IS_WINDOWS)
		return _InterlockedExchangeAdd(reinterpret_cast<volatile long*>(pDest), val);
#endif
	}

#if defined(PLATFORM_IS_64BIT)
	template <class T,
	          ENABLE_IF(std::is_integral<T>::value && sizeof(T) == 8)>
	T FetchAdd(T *pDest, T val)
	{
#if defined(PLATFORM_GNUC_LIKE)
		return __atomic_fetch_add(pDest, val, __ATOMIC_ACQ_REL);
#elif defined(PLATFORM_IS_WINDOWS)
		return _InterlockedExchangeAdd64(reinterpret_cast<volatile long long*>(pDest), val);
#endif
	}
#endif

	//////////////////////////////////////////////////////////////////////////
	template <class T,
	          ENABLE_IF(std::is_integral<T>::value && sizeof(T) == 1)>
	T FetchAnd(T *pDest, T maskVal)
	{
#if defined(PLATFORM_GNUC_LIKE)
		return __atomic_fetch_and(pDest, maskVal, __ATOMIC_ACQ_REL);
#elif defined(PLATFORM_IS_WINDOWS)
		return _InterlockedAnd8(reinterpret_cast<volatile char*>(pDest), maskVal);
#endif
	}

	template <class T,
	          ENABLE_IF(std::is_integral<T>::value && sizeof(T) == 2)>
	T FetchAnd(T *pDest, T maskVal)
	{
#if defined(PLATFORM_GNUC_LIKE)
		return __atomic_fetch_and(pDest, maskVal, __ATOMIC_ACQ_REL);
#elif defined(PLATFORM_IS_WINDOWS)
		return _InterlockedAnd16(reinterpret_cast<volatile short*>(pDest), maskVal);
#endif
	}

	template <class T,
	          ENABLE_IF(std::is_integral<T>::value && sizeof(T) == 4)>
	T FetchAnd(T *pDest, T maskVal)
	{
#if defined(PLATFORM_GNUC_LIKE)
		return __atomic_fetch_and(pDest, maskVal, __ATOMIC_ACQ_REL);
#elif defined(PLATFORM_IS_WINDOWS)
		return _InterlockedAnd(reinterpret_cast<volatile long*>(pDest), maskVal);
#endif
	}

#if defined(PLATFORM_IS_64BIT)
	template <class T,
	          ENABLE_IF(std::is_integral<T>::value && sizeof(T) == 8)>
	T FetchAnd(T *pDest, T maskVal)
	{
#if defined(PLATFORM_GNUC_LIKE)
		return __atomic_fetch_and(pDest, maskVal, __ATOMIC_ACQ_REL);
#elif defined(PLATFORM_IS_WINDOWS)
		return _InterlockedAnd64(reinterpret_cast<volatile long long*>(pDest), maskVal);
#endif
	}
#endif

	//////////////////////////////////////////////////////////////////////////
	template <class T,
	          ENABLE_IF(std::is_integral<T>::value && sizeof(T) == 1)>
	T FetchOr(T *pDest, T maskVal)
	{
#if defined(PLATFORM_GNUC_LIKE)
		return __atomic_fetch_or(pDest, maskVal, __ATOMIC_ACQ_REL);
#elif defined(PLATFORM_IS_WINDOWS)
		return _InterlockedOr8(reinterpret_cast<volatile char*>(pDest), maskVal);
#endif
	}

	template <class T,
	          ENABLE_IF(std::is_integral<T>::value && sizeof(T) == 2)>
	T FetchOr(T *pDest, T maskVal)
	{
#if defined(PLATFORM_GNUC_LIKE)
		return __atomic_fetch_or(pDest, maskVal, __ATOMIC_ACQ_REL);
#elif defined(PLATFORM_IS_WINDOWS)
		return _InterlockedOr16(reinterpret_cast<volatile short*>(pDest), maskVal);
#endif
	}

	template <class T,
	          ENABLE_IF(std::is_integral<T>::value && sizeof(T) == 4)>
	T FetchOr(T *pDest, T maskVal)
	{
#if defined(PLATFORM_GNUC_LIKE)
		return __atomic_fetch_or(pDest, maskVal, __ATOMIC_ACQ_REL);
#elif defined(PLATFORM_IS_WINDOWS)
		return _InterlockedOr(reinterpret_cast<volatile long*>(pDest), maskVal);
#endif
	}

#if defined(PLATFORM_IS_64BIT)
	template <class T,
	          ENABLE_IF(std::is_integral<T>::value && sizeof(T) == 8)>
	T FetchOr(T *pDest, T maskVal)
	{
#if defined(PLATFORM_GNUC_LIKE)
		return __atomic_fetch_or(pDest, maskVal, __ATOMIC_ACQ_REL);
#elif defined(PLATFORM_IS_WINDOWS)
		return _InterlockedOr64(reinterpret_cast<volatile long long*>(pDest), maskVal);
#endif
	}
#endif

	//////////////////////////////////////////////////////////////////////////
	template <class T,
	          ENABLE_IF(std::is_integral<T>::value && sizeof(T) == 4)>
	bool FetchBitSet(T *pDest, uint32_t bitPos)
	{
#if defined(PLATFORM_GNUC_LIKE)
#  if defined(__clang__) && defined(PLATFORM_ARCH_X86)
		bool res = false;
#    if defined(__GCC_ASM_FLAG_OUTPUTS__)
		__asm__ __volatile__
		(
			"lock btsl %[bitPos], %[mem]\n\t"
			: [mem] "+m" (*pDest), [res] "=@ccc" (res)
			: [bitPos] "Kq" (bitPos)
			: "memory"
		);
#    else
		__asm__ __volatile__
		(
			"lock btsl %[bitPos], %[mem]\n\t"
			"setc %[res]\n\t"
			: [mem] "+m" (*pDest), [res] "+q" (res)
			: [bitPos] "Kq" (bitPos)
			: "memory"
		);
#    endif
		return res;
#  else
		T mask = (T)1 << bitPos;
		return !!(__atomic_fetch_or(pDest, mask, __ATOMIC_ACQ_REL) & mask);
#  endif
#elif defined(PLATFORM_IS_WINDOWS)
		return !!_interlockedbittestandset(reinterpret_cast<volatile long*>(pDest), bitPos);
#endif
	}

#if defined(PLATFORM_IS_64BIT)
	template <class T,
	          ENABLE_IF(std::is_integral<T>::value && sizeof(T) == 8)>
	bool FetchBitSet(T *pDest, uint32_t bitPos)
	{
#if defined(PLATFORM_GNUC_LIKE)
#  if defined(__clang__) && defined(PLATFORM_ARCH_X86)
		bool res = false;
#    if defined(__GCC_ASM_FLAG_OUTPUTS__)
		__asm__ __volatile__
		(
			"lock btsq %[bitPos], %[mem]\n\t"
			: [mem] "+m" (*pDest), [res] "=@ccc" (res)
			: [bitPos] "Kq" (bitPos)
			: "memory"
		);
#    else
		__asm__ __volatile__
		(
			"lock btsq %[bitPos], %[mem]\n\t"
			"setc %[res]\n\t"
			: [mem] "+m" (*pDest), [res] "+q" (res)
			: [bitPos] "Kq" (bitPos)
			: "memory"
		);
#    endif
		return res;
#  else
		T mask = (T)1 << bitPos;
		return !!(__atomic_fetch_or(pDest, mask, __ATOMIC_ACQ_REL) & mask);
#  endif
#elif defined(PLATFORM_IS_WINDOWS)
		return !!_interlockedbittestandset64(reinterpret_cast<volatile long long*>(pDest), bitPos);
#endif
	}
#endif

	//////////////////////////////////////////////////////////////////////////
	template <class T,
	          ENABLE_IF(std::is_integral<T>::value && sizeof(T) == 4)>
	bool FetchBitClear(T *pDest, uint32_t bitPos)
	{
#if defined(PLATFORM_GNUC_LIKE)
#  if defined(__clang__) && defined(PLATFORM_ARCH_X86)
		bool res = false;
#    if defined(__GCC_ASM_FLAG_OUTPUTS__)
		__asm__ __volatile__
		(
			"lock btrl %[bitPos], %[mem]\n\t"
			: [mem] "+m" (*pDest), [res] "=@ccc" (res)
			: [bitPos] "Kq" (bitPos)
			: "memory"
		);
#    else
		__asm__ __volatile__
		(
			"lock btrl %[bitPos], %[mem]\n\t"
			"setc %[res]\n\t"
			: [mem] "+m" (*pDest), [res] "+q" (res)
			: [bitPos] "Kq" (bitPos)
			: "memory"
		);
#    endif
		return res;
#  else
		T mask = (T)1 << bitPos;
		return !!(__atomic_fetch_and(pDest, ~mask, __ATOMIC_ACQ_REL) & mask);
#  endif
#elif defined(PLATFORM_IS_WINDOWS)
		return !!_interlockedbittestandreset(reinterpret_cast<volatile long*>(pDest), bitPos);
#endif
	}

#if defined(PLATFORM_IS_64BIT)
	template <class T,
	          ENABLE_IF(std::is_integral<T>::value && sizeof(T) == 8)>
	bool FetchBitClear(T *pDest, uint32_t bitPos)
	{
#if defined(PLATFORM_GNUC_LIKE)
#  if defined(__clang__) && defined(PLATFORM_ARCH_X86)
		bool res = false;
#    if defined(__GCC_ASM_FLAG_OUTPUTS__)
		__asm__ __volatile__
		(
			"lock btrq %[bitPos], %[mem]\n\t"
			: [mem] "+m" (*pDest), [res] "=@ccc" (res)
			: [bitPos] "Kq" (bitPos)
			: "memory"
		);
#    else
		__asm__ __volatile__
		(
			"lock btrq %[bitPos], %[mem]\n\t"
			"setc %[res]\n\t"
			: [mem] "+m" (*pDest), [res] "+q" (res)
			: [bitPos] "Kq" (bitPos)
			: "memory"
		);
#    endif
		return res;
#  else
		T mask = (T)1 << bitPos;
		return !!(__atomic_fetch_and(pDest, ~mask, __ATOMIC_ACQ_REL) & mask);
#  endif
#elif defined(PLATFORM_IS_WINDOWS)
		return !!_interlockedbittestandreset64(reinterpret_cast<volatile long long*>(pDest), bitPos);
#endif
	}
#endif
}
