#pragma once

#include "ui/app.h"
#include "x86/x86.h"

namespace ui {

	struct listing_names : x86::symbolizer {

		const app* owner = nullptr;

		bool name ( uint64_t address, bool code, wchar_t* out, int32_t room ) override {

			return owner->symbol_name ( address, code, out, room );

		}

		bool number ( uint64_t at, uint64_t raw, uint8_t size, wchar_t* out, int32_t room ) override {

			return owner->number_text ( at, raw, size, out, room );

		}

	};

	struct token_line : x86::printer {

		wchar_t text [ 400 ];
		int32_t length = 0;
		token_run runs [ 48 ];
		int32_t count = 0;
		int32_t operand_start = -1;
		const app* owner = nullptr;
		const analysis::database* db = nullptr;

		void put ( const wchar_t* value, int32_t size, x86::token type, uint64_t target ) override {

			if ( type == x86::token::TEXT && operand_start < 0 ) {

				operand_start = length;

				return;

			}

			if ( length + size >= 399 || count >= 48 )
				return;

			tone color = tone::INK;

			switch ( type ) {

				case x86::token::MNEMONIC:
					color = tone::MNEMONIC;
					break;

				case x86::token::PREFIX:
					color = tone::KEYWORD;
					break;

				case x86::token::REGISTER:
					color = tone::REGISTER;
					break;

				case x86::token::NUMBER:
					color = tone::NUMBER;
					break;

				case x86::token::ADDRESS:
					color = tone::ADDRESS;
					break;

				case x86::token::SIZE:
					color = tone::INK_FAINT;
					break;

				case x86::token::PUNCT:
					color = tone::OPERATOR;
					break;

				case x86::token::SYMBOL:

					color = tone::GLOBAL;

					if ( db ) {

						if ( db->function_starting ( target ) >= 0 ) {

							const analysis::function_info& item = db->functions [ static_cast < size_t > ( db->function_starting ( target ) ) ];

							color = ( item.flags & analysis::THUNK ) ? tone::IMPORT : tone::FUNCTION;

						} else if ( db->is_start ( target ) )
							color = tone::LABEL;
						else if ( owner && owner->string_at ( target ) >= 0 )
							color = tone::STRING;

					}

					for ( const loader::import& item : owner->file.imports ) {

						if ( item.slot == target )
							color = tone::IMPORT;

					}

					break;

				default:
					break;

			}

			token_run& run = runs [ count++ ];

			run.start = length;
			run.length = size;
			run.color = color;
			run.target = type == x86::token::SYMBOL || type == x86::token::ADDRESS ? target : 0;

			for ( int32_t i = 0; i < size; i++ )
				text [ length++ ] = value [ i ];

			text [ length ] = 0;

		}

	};

	inline tone mnemonic_tone ( const x86::instruction& ins ) {

		switch ( ins.control ) {

			case x86::flow::CALL:
				return tone::FUNCTION;

			case x86::flow::JUMP:
			case x86::flow::BRANCH:
			case x86::flow::RETURN:
				return tone::CONTROL;

			case x86::flow::STOP:
				return tone::DANGER;

			default:
				break;

		}

		if ( ins.name == x86::mnemonic::nop || ins.name == x86::mnemonic::int3 )
			return tone::INK_FAINT;

		return tone::MNEMONIC;

	}

}