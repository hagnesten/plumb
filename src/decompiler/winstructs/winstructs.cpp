#include "decompiler/winstructs/winstructs.h"
#include "base/strings/strings.h"

const decompiler::builtin_field* decompiler::builtin_field_at ( const wchar_t* owner, int64_t offset, int32_t& index ) {

	static const builtin_field fields [ ] = {

		{ L"_EXCEPTION_POINTERS", 0, 8, 1, L"ExceptionRecord", L"PEXCEPTION_RECORD", L"_EXCEPTION_RECORD" },
		{ L"_EXCEPTION_POINTERS", 8, 8, 1, L"ContextRecord", L"PCONTEXT", L"_CONTEXT" },
		{ L"_EXCEPTION_RECORD", 0, 4, 1, L"ExceptionCode", L"DWORD", nullptr },
		{ L"_EXCEPTION_RECORD", 4, 4, 1, L"ExceptionFlags", L"DWORD", nullptr },
		{ L"_EXCEPTION_RECORD", 8, 8, 1, L"ExceptionRecord", L"struct _EXCEPTION_RECORD *", L"_EXCEPTION_RECORD" },
		{ L"_EXCEPTION_RECORD", 16, 8, 1, L"ExceptionAddress", L"PVOID", nullptr },
		{ L"_EXCEPTION_RECORD", 24, 4, 1, L"NumberParameters", L"DWORD", nullptr },
		{ L"_EXCEPTION_RECORD", 32, 8, 15, L"ExceptionInformation", L"ULONG_PTR", nullptr },
		{ L"_UNICODE_STRING", 0, 2, 1, L"Length", L"USHORT", nullptr },
		{ L"_UNICODE_STRING", 2, 2, 1, L"MaximumLength", L"USHORT", nullptr },
		{ L"_UNICODE_STRING", 8, 8, 1, L"Buffer", L"PWSTR", nullptr },
		{ L"_LIST_ENTRY", 0, 8, 1, L"Flink", L"struct _LIST_ENTRY *", L"_LIST_ENTRY" },
		{ L"_LIST_ENTRY", 8, 8, 1, L"Blink", L"struct _LIST_ENTRY *", L"_LIST_ENTRY" }

	};

	if ( !owner )
		return nullptr;

	for ( const builtin_field& item : fields ) {

		if ( !base::same_text ( item.owner, owner ) || offset < item.offset || offset >= item.offset + static_cast < int64_t > ( item.size ) * item.count )
			continue;

		int64_t delta = offset - item.offset;

		if ( delta % item.size )
			return nullptr;

		index = static_cast < int32_t > ( delta / item.size );

		return &item;

	}

	return nullptr;

}
