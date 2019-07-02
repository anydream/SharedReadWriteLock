#pragma once

#include "Predefines.hpp"

namespace Atomic
{
#if defined(PLATFORM_GNUC_LIKE)
	template <class T>
	T CompareExchange(T *pDest, T cmpVal, T newVal)
	{
		return __sync_val_compare_and_swap(pDest, cmpVal, newVal);
	}

	template <class T>
	T Exchange(T *pDest, T val)
	{
		return __atomic_exchange_n(pDest, val, __ATOMIC_ACQ_REL);
	}

	template <class T>
	T IncrementFetch(T *pDest)
	{
		return __atomic_add_fetch(pDest, 1, __ATOMIC_ACQ_REL);
	}
	
	template <class T>
	T DecrementFetch(T *pDest)
	{
		return __atomic_sub_fetch(pDest, 1, __ATOMIC_ACQ_REL);
	}
	
	template <class T>
	T FetchAdd(T *pDest, T val)
	{
		return __atomic_fetch_add(pDest, val, __ATOMIC_ACQ_REL);
	}
	
	template <class T>
	T FetchAnd(T *pDest, T maskVal)
	{
		return __atomic_fetch_and(pDest, maskVal, __ATOMIC_ACQ_REL);
	}
	
	template <class T>
	T FetchOr(T *pDest, T maskVal)
	{
		return __atomic_fetch_or(pDest, maskVal, __ATOMIC_ACQ_REL);
	}

#  if defined(__clang__) && defined(PLATFORM_ARCH_X86)
	inline __attribute__((always_inline)) bool atomic_bit_test_and_set(uint32_t* ptr, uint32_t bit)
	{
#    if defined(__GCC_ASM_FLAG_OUTPUTS__)
		bool res;

		__asm__ __volatile__
		(
			"lock btsl %[bit], %[mem]\n\t"
			: [mem] "+m" (*ptr), [res] "=@ccc" (res)
			: [bit] "Kq" (bit)
			: "memory"
		);
#    else
		bool res = false;

		__asm__ __volatile__
		(
			"lock btsl %[bit], %[mem]\n\t"
			"setc %[res]\n\t"
			: [mem] "+m" (*ptr), [res] "+q" (res)
			: [bit] "Kq" (bit)
			: "memory"
		);
#    endif
		return res;
	}

	inline __attribute__((always_inline)) bool atomic_bit_test_and_reset(uint32_t* ptr, uint32_t bit)
	{
#    if defined(__GCC_ASM_FLAG_OUTPUTS__)
		bool res;

		__asm__ __volatile__
		(
			"lock btrl %[bit], %[mem]\n\t"
			: [mem] "+m" (*ptr), [res] "=@ccc" (res)
			: [bit] "Kq" (bit)
			: "memory"
		);
#    else
		bool res = false;

		__asm__ __volatile__
		(
			"lock btrl %[bit], %[mem]\n\t"
			"setc %[res]\n\t"
			: [mem] "+m" (*ptr), [res] "+q" (res)
			: [bit] "Kq" (bit)
			: "memory"
		);
#    endif
		return res;
	}

	template <class T,
			  ENABLE_IF(sizeof(T) == 4)>
	bool FetchBitSet(T *pDest, uint32_t bitPos)
	{
		return atomic_bit_test_and_set(pDest, bitPos);
	}

	template <class T,
			  ENABLE_IF(sizeof(T) == 4)>
	bool FetchBitClear(T *pDest, uint32_t bitPos)
	{
		return atomic_bit_test_and_reset(pDest, bitPos);
	}

#    if defined(PLATFORM_IS_64BIT)
	inline __attribute__((always_inline)) bool atomic_bit_test_and_set64(uint64_t* ptr, uint64_t bit)
	{
#    if defined(__GCC_ASM_FLAG_OUTPUTS__)
		bool res;

		__asm__ __volatile__
		(
			"lock btsq %[bit], %[mem]\n\t"
			: [mem] "+m" (*ptr), [res] "=@ccc" (res)
			: [bit] "Kq" (bit)
			: "memory"
		);
#    else
		bool res = false;

		__asm__ __volatile__
		(
			"lock btsq %[bit], %[mem]\n\t"
			"setc %[res]\n\t"
			: [mem] "+m" (*ptr), [res] "+q" (res)
			: [bit] "Kq" (bit)
			: "memory"
		);
#    endif
		return res;
	}

	inline __attribute__((always_inline)) bool atomic_bit_test_and_reset64(uint64_t* ptr, uint64_t bit)
	{
#    if defined(__GCC_ASM_FLAG_OUTPUTS__)
		bool res;

		__asm__ __volatile__
		(
			"lock btrq %[bit], %[mem]\n\t"
			: [mem] "+m" (*ptr), [res] "=@ccc" (res)
			: [bit] "Kq" (bit)
			: "memory"
		);
#    else
		bool res = false;

		__asm__ __volatile__
		(
			"lock btrq %[bit], %[mem]\n\t"
			"setc %[res]\n\t"
			: [mem] "+m" (*ptr), [res] "+q" (res)
			: [bit] "Kq" (bit)
			: "memory"
		);
#    endif
		return res;
	}

	template <class T,
			  ENABLE_IF(sizeof(T) == 8)>
	bool FetchBitSet(T *pDest, uint32_t bitPos)
	{
		return atomic_bit_test_and_set64((uint64_t*)pDest, bitPos);
	}

	template <class T,
			  ENABLE_IF(sizeof(T) == 8)>
	bool FetchBitClear(T *pDest, uint32_t bitPos)
	{
		return atomic_bit_test_and_reset64((uint64_t*)pDest, bitPos);
	}
#    endif

#  else
	template <class T>
	bool FetchBitSet(T *pDest, uint32_t bitPos)
	{
		T mask = (T)1 << bitPos;
		return !!(__atomic_fetch_or(pDest, mask, __ATOMIC_ACQ_REL) & mask);
	}
	
	template <class T>
	bool FetchBitClear(T *pDest, uint32_t bitPos)
	{
		T mask = (T)1 << bitPos;
		return !!(__atomic_fetch_and(pDest, ~mask, __ATOMIC_ACQ_REL) & mask);
	}
#  endif
#elif defined(PLATFORM_IS_WINDOWS)
	template <class T>
	T CompareExchange(T *pDest, T cmpVal, T newVal) = delete;

	template <>
	inline uint8_t CompareExchange(uint8_t *pDest, uint8_t cmpVal, uint8_t newVal)
	{
		return _InterlockedCompareExchange8(reinterpret_cast<volatile char*>(pDest), newVal, cmpVal);
	}

	template <>
	inline uint16_t CompareExchange(uint16_t *pDest, uint16_t cmpVal, uint16_t newVal)
	{
		return _InterlockedCompareExchange16(reinterpret_cast<volatile short*>(pDest), newVal, cmpVal);
	}

	template <>
	inline uint32_t CompareExchange(uint32_t *pDest, uint32_t cmpVal, uint32_t newVal)
	{
		return _InterlockedCompareExchange(reinterpret_cast<volatile long*>(pDest), newVal, cmpVal);
	}

	template <>
	inline uint64_t CompareExchange(uint64_t *pDest, uint64_t cmpVal, uint64_t newVal)
	{
		return _InterlockedCompareExchange64(reinterpret_cast<volatile long long*>(pDest), newVal, cmpVal);
	}

	template <class T>
	T Exchange(T *pDest, T val) = delete;

	template <>
	inline uint8_t Exchange(uint8_t *pDest, uint8_t val)
	{
		return _InterlockedExchange8(reinterpret_cast<volatile char*>(pDest), val);
	}

	template <>
	inline uint16_t Exchange(uint16_t *pDest, uint16_t val)
	{
		return _InterlockedExchange16(reinterpret_cast<volatile short*>(pDest), val);
	}

	template <>
	inline uint32_t Exchange(uint32_t *pDest, uint32_t val)
	{
		return _InterlockedExchange(reinterpret_cast<volatile long*>(pDest), val);
	}

#  if defined(PLATFORM_IS_64BIT)
	template <>
	inline uint64_t Exchange(uint64_t *pDest, uint64_t val)
	{
		return _InterlockedExchange64(reinterpret_cast<volatile long long*>(pDest), val);
	}
#endif

	template <class T>
	T IncrementFetch(T *pDest) = delete;

	template <>
	inline uint16_t IncrementFetch(uint16_t *pDest)
	{
		return _InterlockedIncrement16(reinterpret_cast<volatile short*>(pDest));
	}

	template <>
	inline uint32_t IncrementFetch(uint32_t *pDest)
	{
		return _InterlockedIncrement(reinterpret_cast<volatile long*>(pDest));
	}

#  if defined(PLATFORM_IS_64BIT)
	template <>
	inline uint64_t IncrementFetch(uint64_t *pDest)
	{
		return _InterlockedIncrement64(reinterpret_cast<volatile long long*>(pDest));
	}
#  endif

	template <class T>
	T DecrementFetch(T *pDest) = delete;

	template <>
	inline uint16_t DecrementFetch(uint16_t *pDest)
	{
		return _InterlockedDecrement16(reinterpret_cast<volatile short*>(pDest));
	}

	template <>
	inline uint32_t DecrementFetch(uint32_t *pDest)
	{
		return _InterlockedDecrement(reinterpret_cast<volatile long*>(pDest));
	}

#  if defined(PLATFORM_IS_64BIT)
	template <>
	inline uint64_t DecrementFetch(uint64_t *pDest)
	{
		return _InterlockedDecrement64(reinterpret_cast<volatile long long*>(pDest));
	}
#  endif

	template <class T>
	T FetchAdd(T *pDest, T val) = delete;

	template <>
	inline uint8_t FetchAdd(uint8_t *pDest, uint8_t val)
	{
		return _InterlockedExchangeAdd8(reinterpret_cast<volatile char*>(pDest), val);
	}

	template <>
	inline uint16_t FetchAdd(uint16_t *pDest, uint16_t val)
	{
		return _InterlockedExchangeAdd16(reinterpret_cast<volatile short*>(pDest), val);
	}

	template <>
	inline uint32_t FetchAdd(uint32_t *pDest, uint32_t val)
	{
		return _InterlockedExchangeAdd(reinterpret_cast<volatile long*>(pDest), val);
	}

#  if defined(PLATFORM_IS_64BIT)
	template <>
	inline uint64_t FetchAdd(uint64_t *pDest, uint64_t val)
	{
		return _InterlockedExchangeAdd64(reinterpret_cast<volatile long long*>(pDest), val);
	}
#  endif

	template <class T>
	T FetchAnd(T *pDest, T maskVal) = delete;

	template <>
	inline uint8_t FetchAnd(uint8_t *pDest, uint8_t maskVal)
	{
		return _InterlockedAnd8(reinterpret_cast<volatile char*>(pDest), maskVal);
	}

	template <>
	inline uint16_t FetchAnd(uint16_t *pDest, uint16_t maskVal)
	{
		return _InterlockedAnd16(reinterpret_cast<volatile short*>(pDest), maskVal);
	}

	template <>
	inline uint32_t FetchAnd(uint32_t *pDest, uint32_t maskVal)
	{
		return _InterlockedAnd(reinterpret_cast<volatile long*>(pDest), maskVal);
	}

#  if defined(PLATFORM_IS_64BIT)
	template <>
	inline uint64_t FetchAnd(uint64_t *pDest, uint64_t maskVal)
	{
		return _InterlockedAnd64(reinterpret_cast<volatile long long*>(pDest), maskVal);
	}
#  endif

	template <class T>
	T FetchOr(T *pDest, T maskVal) = delete;

	template <>
	inline uint8_t FetchOr(uint8_t *pDest, uint8_t maskVal)
	{
		return _InterlockedOr8(reinterpret_cast<volatile char*>(pDest), maskVal);
	}

	template <>
	inline uint16_t FetchOr(uint16_t *pDest, uint16_t maskVal)
	{
		return _InterlockedOr16(reinterpret_cast<volatile short*>(pDest), maskVal);
	}

	template <>
	inline uint32_t FetchOr(uint32_t *pDest, uint32_t maskVal)
	{
		return _InterlockedOr(reinterpret_cast<volatile long*>(pDest), maskVal);
	}

#  if defined(PLATFORM_IS_64BIT)
	template <>
	inline uint64_t FetchOr(uint64_t *pDest, uint64_t maskVal)
	{
		return _InterlockedOr64(reinterpret_cast<volatile long long*>(pDest), maskVal);
	}
#  endif

	template <class T>
	bool FetchBitSet(T *pDest, uint32_t bitPos) = delete;

	template <>
	inline bool FetchBitSet(uint32_t *pDest, uint32_t bitPos)
	{
		return !!_interlockedbittestandset(reinterpret_cast<volatile long*>(pDest), bitPos);
	}

#  if defined(PLATFORM_IS_64BIT)
	template <>
	inline bool FetchBitSet(uint64_t *pDest, uint32_t bitPos)
	{
		return !!_interlockedbittestandset64(reinterpret_cast<volatile long long*>(pDest), bitPos);
	}
#  endif

	template <class T>
	bool FetchBitClear(T *pDest, uint32_t bitPos) = delete;

	template <>
	inline bool FetchBitClear(uint32_t *pDest, uint32_t bitPos)
	{
		return !!_interlockedbittestandreset(reinterpret_cast<volatile long*>(pDest), bitPos);
	}

#  if defined(PLATFORM_IS_64BIT)
	template <>
	inline bool FetchBitClear(uint64_t *pDest, uint32_t bitPos)
	{
		return !!_interlockedbittestandreset64(reinterpret_cast<volatile long long*>(pDest), bitPos);
	}
#  endif
#endif
}
