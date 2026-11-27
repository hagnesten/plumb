#pragma once

#include "loader/binary/binary.h"
#include "base/writer/writer.h"

namespace loader {

	bool load_symbols ( binary& file, base::writer& report );

	bool read_pdb ( binary& file, const wchar_t* path, base::writer& report );

	void undecorate ( const char* name, bool x86, wchar_t* out, int32_t room );

	struct msf {

		const uint8_t* data = nullptr;
		uint64_t size = 0;
		uint32_t block = 0;
		uint32_t streams = 0;
		base::list < uint32_t > directory;
		base::list < uint32_t > sizes;
		base::list < uint32_t > firsts;

		uint32_t read32 ( uint64_t at ) const;

		bool open ( );

		bool stream ( uint32_t index, base::list < uint8_t >& out ) const;

	};

	uint16_t get16 ( const base::list < uint8_t >& bytes, size_t at );

	uint32_t get32 ( const base::list < uint8_t >& bytes, size_t at );

	struct name_reader {

		const char* at;
		const char* end;
		const char* backrefs [ 10 ];
		int32_t backref_lengths [ 10 ];
		int32_t backref_count = 0;

		bool fragment ( const char*& text, int32_t& length ) {

			if ( at >= end )
				return false;

			if ( *at >= '0' && *at <= '9' ) {

				int32_t index = *at - '0';

				at++;

				if ( index >= backref_count )
					return false;

				text = backrefs [ index ];
				length = backref_lengths [ index ];

				return true;

			}

			if ( *at == '?' )
				return false;

			text = at;

			while ( at < end && *at != '@' )
				at++;

			if ( at >= end )
				return false;

			length = static_cast < int32_t > ( at - text );
			at++;

			if ( backref_count < 10 ) {

				backrefs [ backref_count ] = text;
				backref_lengths [ backref_count ] = length;
				backref_count++;

			}

			return length > 0;

		}

	};

	void copy_plain ( const char* name, int32_t length, wchar_t* out, int32_t room );

	const wchar_t* base_name ( const wchar_t* path );

	bool exists ( const wchar_t* path );

	void cache_path ( const binary& file, const wchar_t* folder, base::writer& out );

}