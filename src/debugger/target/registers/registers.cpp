#include "debugger/target/registers/registers.h"
#include "debugger/core/state/state.h"
#include <windows.h>

const wchar_t* const debugger::names64 [ 18 ] = { L"RAX", L"RCX", L"RDX", L"RBX", L"RSP", L"RBP", L"RSI", L"RDI", L"R8", L"R9", L"R10", L"R11", L"R12", L"R13", L"R14", L"R15", L"RIP", L"RFLAGS" };

const wchar_t* const debugger::names32 [ 18 ] = { L"EAX", L"ECX", L"EDX", L"EBX", L"ESP", L"EBP", L"ESI", L"EDI", L"", L"", L"", L"", L"", L"", L"", L"", L"EIP", L"EFLAGS" };

const wchar_t* debugger::register_name ( int32_t index, bool wide ) {

	if ( index < 0 || index >= 18 )
		return L"";

	return wide ? names64 [ index ] : names32 [ index ];

}

HANDLE debugger::thread_handle ( DWORD id ) {

	for ( const thread_entry& item : threads ) {

		if ( item.id == id )
			return item.handle;

	}

	return nullptr;

}

bool debugger::get_registers ( HANDLE thread, uint64_t* values ) {

	if ( wide_target ) {

		CONTEXT context = { };

		context.ContextFlags = CONTEXT_FULL;

		if ( !GetThreadContext ( thread, &context ) )
			return false;

		const uint64_t list [ 18 ] = { context.Rax, context.Rcx, context.Rdx, context.Rbx, context.Rsp, context.Rbp, context.Rsi, context.Rdi, context.R8, context.R9, context.R10, context.R11, context.R12, context.R13, context.R14, context.R15, context.Rip, context.EFlags };

		for ( int32_t i = 0; i < 18; i++ )
			values [ i ] = list [ i ];

		return true;

	}

	WOW64_CONTEXT context = { };

	context.ContextFlags = WOW64_CONTEXT_FULL;

	if ( !Wow64GetThreadContext ( thread, &context ) )
		return false;

	const uint64_t list [ 18 ] = { context.Eax, context.Ecx, context.Edx, context.Ebx, context.Esp, context.Ebp, context.Esi, context.Edi, 0, 0, 0, 0, 0, 0, 0, 0, context.Eip, context.EFlags };

	for ( int32_t i = 0; i < 18; i++ )
		values [ i ] = list [ i ];

	return true;

}

void debugger::adjust ( HANDLE thread, bool set_pc, uint64_t pc, bool trap ) {

	if ( wide_target ) {

		CONTEXT context = { };

		context.ContextFlags = CONTEXT_FULL;

		if ( !GetThreadContext ( thread, &context ) )
			return;

		if ( set_pc )
			context.Rip = pc;

		if ( trap )
			context.EFlags |= 0x100;

		if ( resume_flag )
			context.EFlags |= 0x10000;

		SetThreadContext ( thread, &context );

		return;

	}

	WOW64_CONTEXT context = { };

	context.ContextFlags = WOW64_CONTEXT_FULL;

	if ( !Wow64GetThreadContext ( thread, &context ) )
		return;

	if ( set_pc )
		context.Eip = static_cast < DWORD > ( pc );

	if ( trap )
		context.EFlags |= 0x100;

	if ( resume_flag )
		context.EFlags |= 0x10000;

	Wow64SetThreadContext ( thread, &context );

}