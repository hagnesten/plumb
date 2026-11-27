#pragma once

#include "decompiler/builder/builder.h"
#include "decompiler/emit/flags/flags.h"
#include "base/text/text.h"
#include "base/list/list.h"

namespace decompiler {

	bool pointee_of ( const wchar_t* type, wchar_t* out, int32_t room );

	void infer_call_types ( builder& b, base::list < base::text >& out );

	void forward_param_types ( builder& b, base::list < base::text >& out );

	const wchar_t* returned_pointer ( const builder& b );

	bool unsigned_result ( const builder& b );

}