#pragma once

typedef __SIZE_TYPE__ runtime_size;

extern "C" {

	void* runtime_allocate ( runtime_size size );

	void* runtime_allocate_zero ( runtime_size size );

	void runtime_release ( void* block );

	void* memset ( void* target, int value, runtime_size count );

	void* memcpy ( void* target, const void* source, runtime_size count );

	void* memmove ( void* target, const void* source, runtime_size count );

	int memcmp ( const void* left, const void* right, runtime_size count );

	runtime_size strlen ( const char* text );

	runtime_size wcslen ( const wchar_t* text );

}

#ifndef __PLACEMENT_NEW_INLINE
#define __PLACEMENT_NEW_INLINE

inline void* operator new ( runtime_size, void* place ) noexcept {

	return place;

}

inline void operator delete ( void*, void* ) noexcept {

}

#endif