#pragma once

#include "Predefines.hpp"

//////////////////////////////////////////////////////////////////////////
#define Assert(_exp)			((_exp) ? (void)0 : (fprintf(stderr, "* Assert Failed in [%s]:%d\n%s\n", __FILE__, __LINE__, #_exp), PLATFORM_TRAP))
#define AssertDo(_exp, _exec)	do { if (!(_exp)) { _exec; (fprintf(stderr, "* Assert Failed in [%s]:%d\n%s\n", __FILE__, __LINE__, #_exp), PLATFORM_TRAP); } } while (false)

#if defined(PLATFORM_IS_DEBUG) && !defined(__RESHARPER__)
#  define AssertDebug(_exp)				Assert(_exp)
#  define AssertDoDebug(_exp, _exec)	AssertDo(_exp, _exec)
#  define LogDebug(_fmt, ...)			printf(_fmt VA_ARGS_EXPAND(__VA_ARGS__))
#else
#  define AssertDebug(_exp)				((void)0)
#  define AssertDoDebug(_exp, _exec)	((void)0)
#  define LogDebug(_fmt, ...)			((void)0)
#endif
