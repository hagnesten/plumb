#include "decompiler/emit/emitter/emitter.h"
#include "decompiler/emit/vectors/vectors.h"
#include "decompiler/emit/flags/flags.h"
#include "decompiler/lift/intrinsics/intrinsics.h"
#include "decompiler/emit/sink/sink.h"
#include "decompiler/literals/literals.h"
#include "decompiler/apis/apis.h"
#include "decompiler/lift/yields/yields.h"
#include "base/strings/strings.h"
#include "base/sort/sort.h"
#include "base/clock/clock.h"

decompiler::emitter::emitter ( builder& owner ) : b ( owner ) {

	out.spaced = owner.names && owner.names->spaced ( );
	out.names = owner.names;
	next_number = owner.params;
	no_ternary = owner.names && owner.names->expand_ternary ( );
	no_casts = owner.names && owner.names->hide_casts ( );
	indexing = !owner.names || owner.names->index_pointers ( );

}

decompiler::emitter::~emitter ( ) {

	delete shape;

}