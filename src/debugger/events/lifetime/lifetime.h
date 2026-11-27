#pragma once

struct _DEBUG_EVENT;
struct _LOAD_DLL_DEBUG_INFO;
struct _UNLOAD_DLL_DEBUG_INFO;

namespace debugger {

	void process_created ( const _DEBUG_EVENT& event );

	void thread_created ( const _DEBUG_EVENT& event );

	void thread_exited ( unsigned long id );

	void dll_loaded ( const _LOAD_DLL_DEBUG_INFO& info );

	void dll_unloaded ( const _UNLOAD_DLL_DEBUG_INFO& info );

}