#include "x86/decoder/decoder.h"
#include "x86/opcodes/opcodes.h"
#include "x86/forms/forms.h"
#include "base/utility/utility.h"

volatile bool x86::ready = false;

void x86::prepare ( ) {

	if ( ready )
		return;

	build_mnemonic_names ( );
	build_register_names ( );
	build_one_byte ( );
	build_two_byte ( );
	build_three_byte ( );
	build_x87 ( );

	for ( int32_t i = 0; i < evex_forms_count; i++ ) {

		const simd_form& e = evex_forms [ i ];

		if ( !evex_count [ e.map ] [ e.opcode ]++ )
			evex_first [ e.map ] [ e.opcode ] = static_cast < int16_t > ( i );

	}

	for ( int32_t i = 0; i < vex_forms_count; i++ ) {

		const simd_form& e = vex_forms [ i ];

		if ( !vex_count [ e.map ] [ e.opcode ]++ )
			vex_first [ e.map ] [ e.opcode ] = static_cast < int16_t > ( i );

	}

	ready = true;

}

int64_t x86::sign_extend ( uint64_t value, int32_t bytes ) {

	int32_t shift = 64 - bytes * 8;

	return shift <= 0 ? static_cast < int64_t > ( value ) : static_cast < int64_t > ( value << shift ) >> shift;

}

uint8_t x86::gpr ( int32_t number, int32_t size, bool rex ) {

	switch ( size ) {

		case 8:
			return static_cast < uint8_t > ( RAX + number );

		case 4:
			return static_cast < uint8_t > ( EAX + number );

		case 2:
			return static_cast < uint8_t > ( AX + number );

		default:
			return static_cast < uint8_t > ( !rex && number >= 4 && number < 8 ? AH + number - 4 : AL + number );

	}

}

bool x86::read_memory ( cursor& in, state& s, instruction& out ) {

	operand& m = s.memory;

	m = operand ( );
	m.type = kind::MEM;
	m.scale = 1;
	m.segment = out.segment;

	if ( s.address_size == 2 ) {

		static const uint8_t bases [ 8 ] = { BX, BX, BP, BP, SI, DI, BP, BX };
		static const uint8_t indexes [ 8 ] = { SI, DI, SI, DI, NONE, NONE, NONE, NONE };

		if ( s.mod == 0 && s.rm == 6 ) {

			out.displacement_offset = static_cast < uint8_t > ( in.at );
			out.displacement_size = 2;
			m.value = static_cast < int64_t > ( in.read ( 2 ) );

			return !in.failed;

		}

		m.base = bases [ s.rm ];
		m.index = indexes [ s.rm ];

		if ( s.mod == 1 ) {

			out.displacement_offset = static_cast < uint8_t > ( in.at );
			out.displacement_size = 1;
			m.value = sign_extend ( in.read ( 1 ), 1 );

		} else if ( s.mod == 2 ) {

			out.displacement_offset = static_cast < uint8_t > ( in.at );
			out.displacement_size = 2;
			m.value = sign_extend ( in.read ( 2 ), 2 );

		}

		return !in.failed;

	}

	int32_t size = s.address_size;
	bool absolute = false;

	if ( s.rm == 4 ) {

		uint8_t sib = in.byte ( );
		int32_t index = ( ( sib >> 3 ) & 7 ) | ( s.rex_x ? 8 : 0 );

		s.sib = sib;
		int32_t base = ( sib & 7 ) | ( s.rex_b ? 8 : 0 );

		m.scale = static_cast < uint8_t > ( 1 << ( sib >> 6 ) );

		if ( index != 4 )
			m.index = gpr ( index, size, true );

		if ( ( sib & 7 ) == 5 && s.mod == 0 )
			absolute = true;
		else
			m.base = gpr ( base, size, true );

	} else if ( s.rm == 5 && s.mod == 0 ) {

		if ( s.long_mode )
			m.base = size == 8 ? RIP : EIP;

		absolute = true;

	} else
		m.base = gpr ( s.rm | ( s.rex_b ? 8 : 0 ), size, true );

	if ( absolute || s.mod == 2 ) {

		out.displacement_offset = static_cast < uint8_t > ( in.at );
		out.displacement_size = 4;
		m.value = sign_extend ( in.read ( 4 ), 4 );

		if ( absolute && m.base == NONE && m.index == NONE && !s.long_mode )
			m.value = static_cast < int64_t > ( static_cast < uint32_t > ( m.value ) );

	} else if ( s.mod == 1 ) {

		out.displacement_offset = static_cast < uint8_t > ( in.at );
		out.displacement_size = 1;
		m.value = sign_extend ( in.read ( 1 ), 1 );

	}

	return !in.failed;

}

uint8_t x86::vector ( int32_t number, int32_t width ) {

	int32_t bank = width >= 64 ? ZMM0 : ( width == 32 ? YMM0 : XMM0 );

	return static_cast < uint8_t > ( bank + ( number & 31 ) );

}

bool x86::build_operand ( uint8_t which, cursor& in, state& s, instruction& out, operand& item ) {

	int32_t reg = s.reg | ( s.rex_r ? 8 : 0 );
	int32_t rm = s.rm | ( s.rex_b ? 8 : 0 );
	int32_t vreg = reg | ( s.rex_r2 ? 16 : 0 );
	int32_t vrm = rm | ( s.evex && s.rex_x && s.mod == 3 ? 16 : 0 );
	int32_t w = s.width;
	uint8_t os = s.operand_size;
	uint8_t y = s.long_mode && s.rex_w ? 8 : 4;
	bool rex = s.rex_present;

	auto make_reg = [ & ] ( uint8_t value ) {

		item.type = kind::REG;
		item.reg = value;
		item.size = register_size ( value );

	};

	auto make_mem = [ & ] ( uint8_t size ) {

		item = s.memory;
		item.size = size;

	};

	auto rm_or_mem = [ & ] ( uint8_t reg_value, uint8_t size ) {

		if ( s.mod == 3 )
			make_reg ( reg_value );
		else
			make_mem ( size );

	};

	auto immediate = [ & ] ( int32_t bytes, bool sign, uint8_t size ) {

		if ( !out.immediate_size ) {

			out.immediate_offset = static_cast < uint8_t > ( in.at );
			out.immediate_size = static_cast < uint8_t > ( bytes );

		}

		uint64_t raw = in.read ( bytes );

		item.type = kind::IMM;
		item.size = size;
		item.value = sign ? sign_extend ( raw, bytes ) : static_cast < int64_t > ( raw );

	};

	switch ( which ) {

		case EB:
			rm_or_mem ( gpr ( rm, 1, rex ), 1 );
			break;

		case EW:
			rm_or_mem ( gpr ( rm, 2, rex ), 2 );
			break;

		case ED:
			rm_or_mem ( gpr ( rm, 4, rex ), 4 );
			break;

		case EQ:
			rm_or_mem ( gpr ( rm, 8, rex ), 8 );
			break;

		case EV:
			rm_or_mem ( gpr ( rm, os, rex ), os );
			break;

		case EY:
			rm_or_mem ( gpr ( rm, y, rex ), y );
			break;

		case ER8:
			rm_or_mem ( gpr ( rm, 4, rex ), 1 );
			break;

		case ER16:
			rm_or_mem ( gpr ( rm, 4, rex ), 2 );
			break;

		case GB:
			make_reg ( gpr ( reg, 1, rex ) );
			break;

		case GW:
			make_reg ( gpr ( reg, 2, rex ) );
			break;

		case GD:
			make_reg ( gpr ( reg, 4, rex ) );
			break;

		case GQ:
			make_reg ( gpr ( reg, 8, rex ) );
			break;

		case GV:
			make_reg ( gpr ( reg, os, rex ) );
			break;

		case GY:
			make_reg ( gpr ( reg, y, rex ) );
			break;

		case BY:
			make_reg ( gpr ( s.vvvv & 15, y, true ) );
			break;

		case RY:
			make_reg ( gpr ( rm, s.long_mode ? 8 : 4, true ) );
			break;

		case M0:
		case MW:
		case MD:
		case MQ:
		case MT:
		case MO:
		case MX:
		case MY:
		case MV:
		case MP:
		case MF: {

			if ( s.mod == 3 )
				return false;

			uint8_t size = 0;

			switch ( which ) {

				case MW:
					size = 2;
					break;

				case MD:
					size = 4;
					break;

				case MQ:
					size = 8;
					break;

				case MT:
					size = 10;
					break;

				case MO:
					size = 16;
					break;

				case MX:
					size = static_cast < uint8_t > ( w );
					break;

				case MY:
					size = y;
					break;

				case MV:
					size = os;
					break;

				case MP:
				case MF:
					size = static_cast < uint8_t > ( os == 2 ? 4 : ( os == 8 ? 10 : 6 ) );
					break;

				default:
					break;

			}

			make_mem ( size );

			break;

		}

		case IB:
			immediate ( 1, false, 1 );
			break;

		case IBS:
			immediate ( 1, true, os );
			break;

		case IW:
			immediate ( 2, false, 2 );
			break;

		case IZ:
			immediate ( os == 2 ? 2 : 4, true, os );
			break;

		case IV:
			immediate ( os, os == 8, os );
			break;

		case JB:
		case JZ: {

			int32_t bytes = which == JB ? 1 : ( !s.long_mode && os == 2 ? 2 : 4 );

			if ( !out.immediate_size ) {

				out.immediate_offset = static_cast < uint8_t > ( in.at );
				out.immediate_size = static_cast < uint8_t > ( bytes );

			}

			item.type = kind::REL;
			item.size = static_cast < uint8_t > ( s.long_mode ? 8 : 4 );
			item.value = sign_extend ( in.read ( bytes ), bytes );

			break;

		}

		case OB:
		case OV: {

			item = operand ( );
			item.type = kind::MEM;
			item.scale = 1;
			item.segment = out.segment;
			item.size = which == OB ? 1 : os;
			out.displacement_offset = static_cast < uint8_t > ( in.at );
			out.displacement_size = s.address_size;
			item.value = static_cast < int64_t > ( in.read ( s.address_size ) );

			break;

		}

		case AP: {

			uint64_t offset_part = in.read ( os == 2 ? 2 : 4 );
			uint64_t selector = in.read ( 2 );

			item.type = kind::FAR;
			item.size = static_cast < uint8_t > ( os == 2 ? 4 : 6 );
			item.value = static_cast < int64_t > ( ( selector << 32 ) | offset_part );

			break;

		}

		case R_AL:
			make_reg ( AL );
			break;

		case R_CL:
			make_reg ( CL );
			break;

		case R_DX:
			make_reg ( DX );
			break;

		case R_AX:
			make_reg ( gpr ( 0, os, rex ) );
			break;

		case R_EAX:
			make_reg ( os == 2 ? AX : EAX );
			break;

		case R_ES:
		case R_CS:
		case R_SS:
		case R_DS:
		case R_FS:
		case R_GS:
			make_reg ( static_cast < uint8_t > ( ES + which - R_ES ) );
			break;

		case ONE:
			item.type = kind::IMM;
			item.size = 1;
			item.value = 1;
			break;

		case XMM0_:
			make_reg ( XMM0 );
			break;

		case ZB:
			make_reg ( gpr ( rm, 1, rex ) );
			break;

		case ZV:
			make_reg ( gpr ( rm, os, rex ) );
			break;

		case ZY:
			make_reg ( gpr ( rm, s.rex_w ? 8 : 4, rex ) );
			break;

		case SW:

			if ( s.reg > 5 )
				return false;

			make_reg ( static_cast < uint8_t > ( ES + s.reg ) );

			break;

		case CRX:
			make_reg ( static_cast < uint8_t > ( CR0 + reg ) );
			break;

		case DRX:
			make_reg ( static_cast < uint8_t > ( DR0 + reg ) );
			break;

		case STI:
			make_reg ( static_cast < uint8_t > ( ST0 + s.rm ) );
			break;

		case ST0_:
			make_reg ( ST0 );
			break;

		case VX:
			make_reg ( vector ( vreg, w ) );
			break;

		case VO:
			make_reg ( vector ( vreg, 16 ) );
			break;

		case VY:
			make_reg ( vector ( vreg, 32 ) );
			break;

		case HX:
			make_reg ( vector ( s.vvvv, w ) );
			break;

		case HO:
			make_reg ( vector ( s.vvvv, 16 ) );
			break;

		case HY:
			make_reg ( vector ( s.vvvv, 32 ) );
			break;

		case UX:

			if ( s.mod != 3 )
				return false;

			make_reg ( vector ( vrm, w ) );

			break;

		case UO:

			if ( s.mod != 3 )
				return false;

			make_reg ( vector ( vrm, 16 ) );

			break;

		case WX:
			rm_or_mem ( vector ( vrm, w ), static_cast < uint8_t > ( w ) );
			break;

		case WY:
			rm_or_mem ( vector ( vrm, 32 ), 32 );
			break;

		case WO:
			rm_or_mem ( vector ( vrm, 16 ), 16 );
			break;

		case WQ:
			rm_or_mem ( vector ( vrm, 16 ), 8 );
			break;

		case WD:
			rm_or_mem ( vector ( vrm, 16 ), 4 );
			break;

		case WW:
			rm_or_mem ( vector ( vrm, 16 ), 2 );
			break;

		case WB:
			rm_or_mem ( vector ( vrm, 16 ), 1 );
			break;

		case WH:
			rm_or_mem ( vector ( vrm, base::max ( w / 2, 16 ) ), static_cast < uint8_t > ( w / 2 ) );
			break;

		case WQ4:
			rm_or_mem ( vector ( vrm, base::max ( w / 4, 16 ) ), static_cast < uint8_t > ( w / 4 ) );
			break;

		case WE:
			rm_or_mem ( vector ( vrm, 16 ), static_cast < uint8_t > ( w / 8 ) );
			break;

		case WQX:
			rm_or_mem ( vector ( vrm, w ), static_cast < uint8_t > ( w == 16 ? 8 : w ) );
			break;

		case WS:
			rm_or_mem ( vector ( vrm, 16 ), static_cast < uint8_t > ( s.rex_w ? 8 : 4 ) );
			break;

		case LX: {

			uint8_t value = in.byte ( );

			if ( !out.immediate_size ) {

				out.immediate_offset = static_cast < uint8_t > ( in.at - 1 );
				out.immediate_size = 1;

			}

			make_reg ( vector ( ( value >> 4 ) & ( s.long_mode ? 15 : 7 ), w ) );

			break;

		}

		case PQ:
			make_reg ( static_cast < uint8_t > ( MM0 + s.reg ) );
			break;

		case QQ:
			rm_or_mem ( static_cast < uint8_t > ( MM0 + s.rm ), 8 );
			break;

		case QD:
			rm_or_mem ( static_cast < uint8_t > ( MM0 + s.rm ), 4 );
			break;

		case NQ:

			if ( s.mod != 3 )
				return false;

			make_reg ( static_cast < uint8_t > ( MM0 + s.rm ) );

			break;

		case KR:
			make_reg ( static_cast < uint8_t > ( K0 + ( s.reg & 7 ) ) );
			break;

		case KV:
			make_reg ( static_cast < uint8_t > ( K0 + ( s.vvvv & 7 ) ) );
			break;

		case KB:

			if ( s.mod != 3 )
				return false;

			make_reg ( static_cast < uint8_t > ( K0 + ( s.rm & 7 ) ) );

			break;

		case KRM:
			rm_or_mem ( static_cast < uint8_t > ( K0 + ( s.rm & 7 ) ), os );
			break;

		case KGR:
			make_reg ( gpr ( reg, os == 8 ? 8 : 4, true ) );
			break;

		case KGB:

			if ( s.mod != 3 )
				return false;

			make_reg ( gpr ( rm, os == 8 ? 8 : 4, true ) );

			break;

		case EWV:

			if ( s.mod == 3 )
				make_reg ( gpr ( rm, os, rex ) );
			else
				make_mem ( 2 );

			break;

		case EZ:
			rm_or_mem ( gpr ( rm, s.long_mode ? 8 : 4, rex ), static_cast < uint8_t > ( s.long_mode ? 8 : 4 ) );
			break;

		case GZ:
			make_reg ( gpr ( reg, s.long_mode ? 8 : 4, rex ) );
			break;

		case GA:
			make_reg ( gpr ( reg, s.address_size, rex ) );
			break;

		case MZ:

			if ( s.mod == 3 )
				return false;

			make_mem ( 64 );

			break;

		case RD:
		case RQ:
		case RA:

			if ( s.mod != 3 )
				return false;

			make_reg ( gpr ( rm, which == RD ? 4 : ( which == RQ ? 8 : s.address_size ), rex ) );

			break;

		case R_AX16:
			make_reg ( AX );
			break;

		default:
			return false;

	}

	return !in.failed;

}

x86::mnemonic x86::amd_3dnow ( uint8_t code ) {

	switch ( code ) {

		case 0x0C:
			return mnemonic::pi2fw;

		case 0x0D:
			return mnemonic::pi2fd;

		case 0x1C:
			return mnemonic::pf2iw;

		case 0x1D:
			return mnemonic::pf2id;

		case 0x8A:
			return mnemonic::pfnacc;

		case 0x8E:
			return mnemonic::pfpnacc;

		case 0x90:
			return mnemonic::pfcmpge;

		case 0x94:
			return mnemonic::pfmin;

		case 0x96:
			return mnemonic::pfrcp;

		case 0x97:
			return mnemonic::pfrsqrt;

		case 0x9A:
			return mnemonic::pfsub;

		case 0x9E:
			return mnemonic::pfadd;

		case 0xA0:
			return mnemonic::pfcmpgt;

		case 0xA4:
			return mnemonic::pfmax;

		case 0xA6:
			return mnemonic::pfrcpit1;

		case 0xA7:
			return mnemonic::pfrsqit1;

		case 0xAA:
			return mnemonic::pfsubr;

		case 0xAE:
			return mnemonic::pfacc;

		case 0xB0:
			return mnemonic::pfcmpeq;

		case 0xB4:
			return mnemonic::pfmul;

		case 0xB6:
			return mnemonic::pfrcpit2;

		case 0xB7:
			return mnemonic::pmulhrw;

		case 0xBB:
			return mnemonic::pswapd;

		case 0xBF:
			return mnemonic::pavgusb;

		default:
			return mnemonic::invalid;

	}

}

bool x86::refine ( form& f, state& s, int32_t map, uint8_t opcode, uint8_t mandatory ) {

	if ( map == 1 && ( opcode == 0x12 || opcode == 0x16 ) && mandatory <= 1 && s.mod == 3 ) {

		f.name = opcode == 0x12 ? mnemonic::movhlps : mnemonic::movlhps;
		f.ops [ 0 ] = VO;
		f.ops [ 1 ] = UO;

	}

	if ( map == 1 && ( opcode == 0x78 || opcode == 0x79 ) && ( mandatory == 1 || mandatory == 3 ) && !s.vex && s.mod != 3 ) {

		f.name = opcode == 0x78 ? mnemonic::vmread : mnemonic::vmwrite;
		f.ops [ 0 ] = opcode == 0x78 ? EZ : GZ;
		f.ops [ 1 ] = opcode == 0x78 ? GZ : EZ;
		f.ops [ 2 ] = NO;
		f.ops [ 3 ] = NO;

	}

	if ( map == 2 && !s.vex && mandatory == 2 ) {

		if ( opcode == 0xDC && s.mod == 3 ) {

			f.name = mnemonic::loadiwkey;
			f.ops [ 0 ] = VO;
			f.ops [ 1 ] = UO;

		}

		if ( opcode == 0xD8 ) {

			if ( s.mod == 3 || s.reg > 3 )
				return false;

			const mnemonic wide [ 4 ] = { mnemonic::aesencwide128kl, mnemonic::aesdecwide128kl, mnemonic::aesencwide256kl, mnemonic::aesdecwide256kl };

			f.name = wide [ s.reg ];

		}

	}

	if ( map == 2 && !s.vex && opcode == 0xF8 && ( mandatory == 2 || mandatory == 3 ) && s.mod == 3 ) {

		f.name = mandatory == 2 ? mnemonic::uwrmsr : mnemonic::urdmsr;
		f.ops [ 0 ] = GQ;
		f.ops [ 1 ] = RQ;

	}

	return true;

}

bool x86::resolve_group ( form& f, state& s, uint8_t mandatory, instruction& out ) {

	const form& g = groups [ f.group ] [ s.reg ];
	uint8_t m = s.mod;
	uint8_t r = s.reg;
	uint8_t low = s.rm;

	auto set = [ & ] ( mnemonic name, uint8_t a, uint8_t b, uint8_t c, uint16_t extra ) {

		f.name = name;
		f.ops [ 0 ] = a;
		f.ops [ 1 ] = b;
		f.ops [ 2 ] = c;
		f.ops [ 3 ] = NO;
		f.flags = static_cast < uint16_t > ( ( f.flags & ~NDS ) | extra );

	};

	switch ( f.group ) {

		case G6:

			if ( r == 6 ) {

				if ( mandatory != 3 || !s.long_mode )
					return false;

				set ( mnemonic::lkgs, EW, NO, NO, NOWRITE );
				s.prefix_used = true;

				return true;

			}

			break;

		case G7:

			if ( m == 3 ) {

				static const mnemonic plain [ 8 ] [ 8 ] = {
					{ mnemonic::enclv, mnemonic::vmcall, mnemonic::vmlaunch, mnemonic::vmresume, mnemonic::vmxoff, mnemonic::pconfig, mnemonic::wrmsrns, mnemonic::pbndkb },
					{ mnemonic::monitor, mnemonic::mwait, mnemonic::clac, mnemonic::stac, mnemonic::invalid, mnemonic::invalid, mnemonic::invalid, mnemonic::encls },
					{ mnemonic::xgetbv, mnemonic::xsetbv, mnemonic::invalid, mnemonic::invalid, mnemonic::vmfunc, mnemonic::xend, mnemonic::xtest, mnemonic::enclu },
					{ mnemonic::vmrun, mnemonic::vmmcall, mnemonic::vmload, mnemonic::vmsave, mnemonic::stgi, mnemonic::clgi, mnemonic::skinit, mnemonic::invlpga },
					{ mnemonic::smsw, mnemonic::smsw, mnemonic::smsw, mnemonic::smsw, mnemonic::smsw, mnemonic::smsw, mnemonic::smsw, mnemonic::smsw },
					{ mnemonic::serialize, mnemonic::invalid, mnemonic::invalid, mnemonic::invalid, mnemonic::invalid, mnemonic::invalid, mnemonic::rdpkru, mnemonic::wrpkru },
					{ mnemonic::lmsw, mnemonic::lmsw, mnemonic::lmsw, mnemonic::lmsw, mnemonic::lmsw, mnemonic::lmsw, mnemonic::lmsw, mnemonic::lmsw },
					{ mnemonic::swapgs, mnemonic::rdtscp, mnemonic::monitorx, mnemonic::mwaitx, mnemonic::clzero, mnemonic::rdpru, mnemonic::invlpgb, mnemonic::tlbsync }
				};

				uint8_t code = static_cast < uint8_t > ( 0xC0 | r << 3 | low );
				mnemonic name = plain [ r ] [ low ];

				if ( !s.long_mode && name == mnemonic::pbndkb )
					return false;

				if ( !s.long_mode && ( code == 0xC6 || code == 0xCA || ( code >= 0xCD && code <= 0xCF ) || ( code >= 0xEC && code <= 0xEF ) || code == 0xFD || code == 0xFE || code == 0xFF ) && mandatory != 0 ) {

					if ( code == 0xFF && mandatory == 3 ) {

						set ( mnemonic::pvalidate, NO, NO, NO, 0 );
						s.prefix_used = true;

						return true;

					}

					if ( code == 0xEC || code == 0xED )
						return false;

				} else if ( mandatory == 1 ) {

					if ( code >= 0xCC && code <= 0xCF ) {

						const mnemonic seam [ 4 ] = { mnemonic::tdcall, mnemonic::seamret, mnemonic::seamops, mnemonic::seamcall };

						name = seam [ code - 0xCC ];
						s.prefix_used = true;

					}

				} else if ( mandatory == 2 ) {

					const uint8_t codes [ 11 ] = { 0xC6, 0xE8, 0xEA, 0xEC, 0xED, 0xEE, 0xEF, 0xFD, 0xFE, 0xFF, 0xCA };
					const mnemonic names_f3 [ 11 ] = { mnemonic::wrmsrlist, mnemonic::setssbsy, mnemonic::saveprevssp, mnemonic::uiret, mnemonic::testui, mnemonic::clui, mnemonic::stui, mnemonic::rmpquery, mnemonic::rmpadjust, mnemonic::psmash, mnemonic::eretu };

					for ( int32_t i = 0; i < 11; i++ ) {

						if ( codes [ i ] == code ) {

							name = names_f3 [ i ];
							s.prefix_used = true;

						}

					}

				} else if ( mandatory == 3 ) {

					const uint8_t codes [ 6 ] = { 0xC6, 0xE8, 0xE9, 0xFE, 0xFF, 0xCA };
					const mnemonic names_f2 [ 6 ] = { mnemonic::rdmsrlist, mnemonic::xsusldtrk, mnemonic::xresldtrk, mnemonic::rmpupdate, mnemonic::pvalidate, mnemonic::erets };

					for ( int32_t i = 0; i < 6; i++ ) {

						if ( codes [ i ] == code ) {

							name = names_f2 [ i ];
							s.prefix_used = true;

						}

					}

				}

				if ( name == mnemonic::smsw )
					set ( name, EV, NO, NO, 0 );
				else if ( name == mnemonic::lmsw )
					set ( name, EW, NO, NO, 0 );
				else
					set ( name, NO, NO, NO, 0 );

				return name != mnemonic::invalid;

			}

			if ( r == 5 ) {

				if ( mandatory != 2 )
					return false;

				set ( mnemonic::rstorssp, MQ, NO, NO, NOWRITE );
				s.prefix_used = true;

				return true;

			}

			break;

		case G9:

			if ( m != 3 ) {

				if ( r == 1 ) {

					set ( s.rex_w ? mnemonic::cmpxchg16b : mnemonic::cmpxchg8b, s.rex_w ? MO : MQ, NO, NO, 0 );

					return true;

				}

				if ( r >= 3 && r <= 5 ) {

					const mnemonic saves [ 3 ] = { mnemonic::xrstors, mnemonic::xsavec, mnemonic::xsaves };

					set ( saves [ r - 3 ], M0, NO, NO, r == 3 ? NOWRITE : 0 );

					return true;

				}

				if ( r == 6 || r == 7 ) {

					mnemonic name = mnemonic::invalid;

					if ( r == 6 )
						name = mandatory == 1 ? mnemonic::vmclear : ( mandatory == 2 ? mnemonic::vmxon : mnemonic::vmptrld );
					else
						name = mnemonic::vmptrst;

					if ( name == mnemonic::invalid )
						return false;

					set ( name, MQ, NO, NO, name == mnemonic::vmptrst ? 0 : NOWRITE );
					s.prefix_used = name == mnemonic::vmclear || name == mnemonic::vmxon;

					return true;

				}

				return false;

			}

			if ( r == 6 ) {

				if ( mandatory == 2 && s.long_mode ) {

					set ( mnemonic::senduipi, RQ, NO, NO, NOWRITE );
					s.prefix_used = true;

					return true;

				}

				set ( mnemonic::rdrand, EV, NO, NO, 0 );

				return true;

			}

			if ( r == 7 ) {

				if ( mandatory == 2 )
					set ( mnemonic::rdpid, RY, NO, NO, 0 );
				else
					set ( mnemonic::rdseed, EV, NO, NO, 0 );

				return true;

			}

			return false;

		case G15:

			if ( m == 3 || ( mandatory == 2 && r == 4 ) ) {

				if ( mandatory == 2 && r == 4 ) {

					set ( mnemonic::ptwrite, EY, NO, NO, NOWRITE );
					s.prefix_used = true;

					return true;

				}

				if ( r == 6 && mandatory != 0 ) {

					if ( mandatory == 1 )
						set ( mnemonic::tpause, RD, NO, NO, NOWRITE );
					else if ( mandatory == 3 )
						set ( mnemonic::umwait, RD, NO, NO, NOWRITE );
					else
						set ( mnemonic::umonitor, RA, NO, NO, NOWRITE );

					s.prefix_used = true;

					return true;

				}

				if ( mandatory == 2 && r < 4 && s.long_mode ) {

					const mnemonic base_ops [ 4 ] = { mnemonic::rdfsbase, mnemonic::rdgsbase, mnemonic::wrfsbase, mnemonic::wrgsbase };

					set ( base_ops [ r ], EY, NO, NO, r >= 2 ? NOWRITE : 0 );

					return true;

				}

				if ( mandatory == 2 && r == 5 ) {

					set ( s.rex_w ? mnemonic::incsspq : mnemonic::incsspd, EY, NO, NO, NOWRITE );

					return true;

				}

				if ( ( r == 5 && mandatory != 2 ) || ( r == 6 && mandatory == 0 ) || r == 7 ) {

					const mnemonic fences [ 3 ] = { mnemonic::lfence, mnemonic::mfence, mnemonic::sfence };

					set ( fences [ r - 5 ], NO, NO, NO, 0 );

					return true;

				}

				return false;

			}

			{

				const mnemonic memory_ops [ 8 ] = { mnemonic::fxsave, mnemonic::fxrstor, mnemonic::ldmxcsr, mnemonic::stmxcsr, mnemonic::xsave, mnemonic::xrstor, mnemonic::xsaveopt, mnemonic::clflush };
				mnemonic name = memory_ops [ r ];

				if ( mandatory == 1 && r == 6 )
					name = mnemonic::clwb;
				else if ( mandatory == 1 && r == 7 )
					name = mnemonic::clflushopt;
				else if ( mandatory == 2 && r == 6 ) {

					set ( mnemonic::clrssbsy, MQ, NO, NO, 0 );
					s.prefix_used = true;

					return true;

				}

				set ( name, r == 2 || r == 3 ? MD : M0, NO, NO, r == 2 || r == 7 || r == 1 || r == 5 ? NOWRITE : 0 );

			}

			return true;

		case G12:
		case G13:
		case G14: {

			if ( m != 3 )
				return false;

			static const mnemonic table [ 3 ] [ 8 ] = {
				{ mnemonic::invalid, mnemonic::invalid, mnemonic::psrlw, mnemonic::invalid, mnemonic::psraw, mnemonic::invalid, mnemonic::psllw, mnemonic::invalid },
				{ mnemonic::invalid, mnemonic::invalid, mnemonic::psrld, mnemonic::invalid, mnemonic::psrad, mnemonic::invalid, mnemonic::pslld, mnemonic::invalid },
				{ mnemonic::invalid, mnemonic::invalid, mnemonic::psrlq, mnemonic::psrldq, mnemonic::invalid, mnemonic::invalid, mnemonic::psllq, mnemonic::pslldq }
			};

			mnemonic name = table [ f.group - G12 ] [ r ];

			if ( name == mnemonic::invalid )
				return false;

			if ( mandatory == 0 ) {

				if ( s.vex || name == mnemonic::psrldq || name == mnemonic::pslldq )
					return false;

				set ( name, NQ, IB, NO, 0 );

			} else if ( s.vex )
				set ( name, HX, UX, IB, 0 );
			else
				set ( name, UX, IB, NO, 0 );

			return true;

		}

		case G16:

			if ( m != 3 && r >= 6 && s.long_mode ) {

				set ( r == 7 ? mnemonic::prefetchit0 : mnemonic::prefetchit1, M0, NO, NO, NOWRITE );

				return true;

			}

			if ( m == 3 || r > 3 ) {

				set ( mnemonic::nop, EV, NO, NO, NOWRITE );

				return true;

			}

			break;

		case G_PREFETCH:

			if ( m == 3 )
				return false;

			set ( r == 1 ? mnemonic::prefetchw : ( r == 2 ? mnemonic::prefetchwt1 : mnemonic::prefetch ), M0, NO, NO, NOWRITE );

			return true;

		case G_HINT:

			if ( s.opcode == 0x1C && m != 3 && r == 0 ) {

				set ( mnemonic::cldemote, M0, NO, NO, NOWRITE );

				return true;

			}

			if ( s.opcode == 0x1E && mandatory == 2 && m == 3 && r == 7 && ( low == 2 || low == 3 ) ) {

				set ( low == 2 ? mnemonic::endbr64 : mnemonic::endbr32, NO, NO, NO, 0 );

				return true;

			}

			if ( s.opcode == 0x1E && mandatory == 2 && m == 3 && r == 1 ) {

				set ( s.rex_w ? mnemonic::rdsspq : mnemonic::rdsspd, EY, NO, NO, 0 );

				return true;

			}

			set ( mnemonic::nop, EV, NO, NO, NOWRITE );

			return true;

		case G11B:
		case G11V:

			if ( m == 3 && r == 7 && low == 0 ) {

				if ( f.group == G11B )
					set ( mnemonic::xabort, IB, NO, NO, NOWRITE );
				else
					set ( mnemonic::xbegin, JZ, NO, NO, NOWRITE );

				return true;

			}

			break;

		default:
			break;

	}

	if ( g.name == mnemonic::invalid )
		return false;

	f.name = g.name;
	f.flags = static_cast < uint16_t > ( f.flags | g.flags );

	if ( g.ops [ 0 ] != NO ) {

		for ( int32_t i = 0; i < 4; i++ )
			f.ops [ i ] = g.ops [ i ];

	}

	return true;

}

bool x86::writes_only ( mnemonic name ) {

	switch ( name ) {

		case mnemonic::mov:
		case mnemonic::movzx:
		case mnemonic::movsx:
		case mnemonic::movsxd:
		case mnemonic::lea:
		case mnemonic::pop:
		case mnemonic::movaps:
		case mnemonic::movapd:
		case mnemonic::movups:
		case mnemonic::movupd:
		case mnemonic::movdqa:
		case mnemonic::movdqu:
		case mnemonic::movd:
		case mnemonic::movq:
		case mnemonic::movss:
		case mnemonic::movsd:
		case mnemonic::movnti:
		case mnemonic::movntdq:
		case mnemonic::movntps:
		case mnemonic::movntpd:
		case mnemonic::lddqu:
		case mnemonic::cvtsi2sd:
		case mnemonic::cvtsi2ss:
		case mnemonic::cvttsd2si:
		case mnemonic::cvttss2si:
		case mnemonic::cvtsd2si:
		case mnemonic::cvtss2si:
		case mnemonic::stmxcsr:
		case mnemonic::fnstcw:
		case mnemonic::fnstsw:
		case mnemonic::fst:
		case mnemonic::fstp:
		case mnemonic::fist:
		case mnemonic::fistp:
		case mnemonic::fisttp:
			return true;

		default:
			return static_cast < int32_t > ( name ) >= static_cast < int32_t > ( mnemonic::seto ) && static_cast < int32_t > ( name ) <= static_cast < int32_t > ( mnemonic::setg );

	}

}

int32_t x86::evex_bytes ( uint8_t size, int32_t width ) {

	switch ( size & 15 ) {

		case 0:
			return width;

		case 1:
			return width / 2;

		case 2:
			return width / 4;

		case 3:
			return width / 8;

		case 4:
			return 1;

		case 5:
			return 2;

		case 6:
		case 11:
			return 4;

		case 7:
		case 12:
			return 8;

		case 8:
			return 16;

		case 9:
			return 32;

		case 10:
			return 64;

		case 14:
			return width == 16 ? 8 : width;

		default:
			return 0;

	}

}

int32_t x86::evex_lanes ( uint8_t size, int32_t width ) {

	switch ( size & 15 ) {

		case 1:
			return base::max ( width / 2, 16 );

		case 2:
			return base::max ( width / 4, 16 );

		default:
			return width;

	}

}

bool x86::decode_vector ( cursor& in, state& s, instruction& out, int32_t map, uint8_t opcode, uint8_t pp, bool addrsize, uint16_t prefixes ) {

	uint8_t modrm = in.byte ( );

	if ( in.failed )
		return false;

	s.has_modrm = true;
	s.mod = modrm >> 6;
	s.reg = ( modrm >> 3 ) & 7;
	s.rm = modrm & 7;
	s.address_size = static_cast < uint8_t > ( s.long_mode ? ( addrsize ? 4 : 8 ) : ( addrsize ? 2 : 4 ) );

	bool round = s.evex && s.broadcast && s.mod == 3;
	int32_t length_code = s.width == 16 ? 0 : ( s.width == 32 ? 1 : ( s.width == 64 ? 2 : 3 ) );
	uint8_t mode = s.mod == 3 ? 2 : 1;
	const simd_form* table = s.evex ? evex_forms : vex_forms;
	const simd_form* form = nullptr;
	int32_t first = s.evex ? evex_first [ map ] [ opcode ] : vex_first [ map & 3 ] [ opcode ];
	int32_t total = s.evex ? evex_count [ map ] [ opcode ] : vex_count [ map & 3 ] [ opcode ];

	bool w_bit = s.rex_w;

	if ( !s.long_mode && w_bit && !s.evex ) {

		for ( int32_t i = first; i < first + total; i++ ) {

			const simd_form& e = table [ i ];

			if ( e.pp != pp || e.w != 1 || ( e.reg >= 0 && e.reg != s.reg ) )
				continue;

			for ( const simd_operand& op : e.ops ) {

				if ( op.kind == 2 && op.size == 12 )
					w_bit = false;

			}

		}

	}

	for ( int32_t i = first; i < first + total && !form; i++ ) {

		const simd_form& e = table [ i ];

		if ( e.pp != pp || e.mode != mode )
			continue;

		if ( e.w != 2 && e.w != ( w_bit ? 1 : 0 ) )
			continue;

		if ( e.reg >= 0 && e.reg != s.reg )
			continue;

		if ( ( e.flags & 32 ) && ( s.rm != 0 || s.rex_b ) )
			continue;

		if ( round ? !( e.flags & 12 ) : !( e.lengths & ( 1 << length_code ) ) )
			continue;

		form = &e;

	}

	if ( !form )
		return false;

	if ( ( out.mask && !( form->flags & 1 ) ) || ( !out.mask && ( form->flags & 16 ) ) || ( out.zeroing && !( form->flags & 2 ) ) )
		return false;

	bool uses_vvvv = false;
	bool vsib = false;

	for ( const simd_operand& op : form->ops ) {

		if ( op.source == 2 )
			uses_vvvv = true;

		if ( op.kind == 5 )
			vsib = true;

	}

	bool index_high = ( s.vvvv & 16 ) != 0;

	if ( !uses_vvvv && ( ( s.vvvv & 15 ) || ( !vsib && index_high ) ) )
		return false;

	if ( s.broadcast && s.mod != 3 && !form->element )
		return false;

	if ( vsib && s.mod != 3 && s.rm != 4 )
		return false;

	int32_t width = round ? 64 : s.width;

	if ( s.mod != 3 && !read_memory ( in, s, out ) )
		return false;

	int32_t reg_number = s.reg | ( s.rex_r ? 8 : 0 ) | ( s.rex_r2 ? 16 : 0 );
	int32_t rm_number = s.rm | ( s.rex_b ? 8 : 0 ) | ( s.evex && s.rex_x && s.mod == 3 ? 16 : 0 );
	int32_t count = 0;
	int32_t suffix = -1;

	auto suffix_byte = [ & ] ( ) {

		if ( suffix < 0 ) {

			if ( !out.immediate_size ) {

				out.immediate_offset = static_cast < uint8_t > ( in.at );
				out.immediate_size = 1;

			}

			suffix = in.byte ( );

		}

		return suffix;

	};

	bool has_is4 = false;

	for ( const simd_operand& op : form->ops )
		has_is4 = has_is4 || op.source == 6;

	for ( int32_t i = 0; i < 5 && form->ops [ i ].source; i++ ) {

		const simd_operand& op = form->ops [ i ];
		operand item;
		int32_t lanes = evex_lanes ( op.size, width );
		uint8_t gpr_size = static_cast < uint8_t > ( op.size == 12 && s.long_mode ? 8 : ( op.size == 15 ? 2 : 4 ) );

		auto make_reg = [ & ] ( uint8_t value ) {

			item.type = kind::REG;
			item.reg = value;
			item.size = register_size ( value );

		};

		switch ( op.source ) {

			case 1:

				if ( op.kind != 0 && op.kind != 2 && ( s.rex_r || s.rex_r2 ) )
					return false;

				if ( op.kind == 0 )
					make_reg ( vector ( reg_number, lanes ) );
				else if ( op.kind == 1 )
					make_reg ( static_cast < uint8_t > ( K0 + ( s.reg & 7 ) ) );
				else if ( op.kind == 4 )
					make_reg ( static_cast < uint8_t > ( K0 + ( s.reg & 6 ) ) );
				else if ( op.kind == 6 )
					make_reg ( static_cast < uint8_t > ( TMM0 + ( s.reg & 7 ) ) );
				else
					make_reg ( gpr ( s.reg | ( s.rex_r ? 8 : 0 ), gpr_size, true ) );

				break;

			case 2:

				if ( op.kind != 0 && s.vvvv > 15 )
					return false;

				if ( op.kind == 0 )
					make_reg ( vector ( s.vvvv, lanes ) );
				else if ( op.kind == 1 )
					make_reg ( static_cast < uint8_t > ( K0 + ( s.vvvv & 7 ) ) );
				else if ( op.kind == 6 )
					make_reg ( static_cast < uint8_t > ( TMM0 + ( s.vvvv & 7 ) ) );
				else
					make_reg ( gpr ( s.vvvv & 15, gpr_size, true ) );

				break;

			case 3:

				if ( s.mod == 3 ) {

					if ( op.kind == 1 && ( s.rex_b || s.rex_x ) )
						return false;

					if ( op.kind == 0 )
						make_reg ( vector ( rm_number, lanes ) );
					else if ( op.kind == 1 )
						make_reg ( static_cast < uint8_t > ( K0 + ( s.rm & 7 ) ) );
					else if ( op.kind == 2 )
						make_reg ( gpr ( s.rm | ( s.rex_b ? 8 : 0 ), gpr_size, true ) );
					else if ( op.kind == 6 )
						make_reg ( static_cast < uint8_t > ( TMM0 + ( s.rm & 7 ) ) );
					else
						return false;

				} else {

					if ( op.kind != 3 && op.kind != 5 )
						return false;

					int32_t bytes = evex_bytes ( op.size, width );

					item = s.memory;
					item.size = static_cast < uint8_t > ( s.broadcast ? form->element : bytes );

					if ( s.broadcast )
						out.broadcast = static_cast < uint8_t > ( bytes / form->element );

					if ( s.evex && out.displacement_size == 1 )
						item.value *= s.broadcast ? form->element : ( form->scale ? form->scale : ( bytes ? bytes : 1 ) );

					if ( op.kind == 5 ) {

						int32_t index = ( ( s.sib >> 3 ) & 7 ) | ( s.rex_x ? 8 : 0 ) | ( s.evex && index_high ? 16 : 0 );

						item.index = vector ( index, evex_lanes ( static_cast < uint8_t > ( op.size >> 4 ), width ) );
						item.scale = static_cast < uint8_t > ( 1 << ( s.sib >> 6 ) );

					}

				}

				break;

			case 4:
				item.type = kind::IMM;
				item.size = 1;
				item.value = static_cast < int64_t > ( has_is4 ? suffix_byte ( ) & 15 : suffix_byte ( ) );
				break;

			case 6:
				make_reg ( vector ( ( suffix_byte ( ) >> 4 ) & ( s.long_mode ? 15 : 7 ), lanes ) );
				break;

			default:
				break;

		}

		item.access = READ;
		out.operands [ count++ ] = item;

		if ( count >= 5 )
			break;

	}

	if ( in.failed )
		return false;

	out.length = static_cast < uint8_t > ( in.at );
	out.count = static_cast < uint8_t > ( count );
	out.name = form->name;
	out.rounding = static_cast < uint8_t > ( round ? ( ( form->flags & 8 ) ? 1 : 2 + length_code ) : 0 );

	if ( count )
		out.operands [ 0 ].access = static_cast < uint8_t > ( writes_only ( form->name ) ? WRITE : READ | WRITE );

	if ( s.rex_w )
		prefixes |= REX_W;

	out.prefixes = prefixes;

	return true;

}

bool x86::decode ( const uint8_t* code, size_t available, uint64_t address, bool long_mode, instruction& out ) {

	if ( !ready )
		prepare ( );

	out = instruction ( );
	out.address = address;

	cursor in = { code, available < 15 ? available : 15, 0, false };
	state s = { };
	bool opsize = false;
	bool addrsize = false;
	uint8_t rep = 0;
	uint8_t rex = 0;
	uint16_t prefixes = 0;
	bool notrack = false;

	s.long_mode = long_mode;
	s.width = 16;

	for ( ;; ) {

		if ( in.at >= in.limit )
			return false;

		uint8_t b = code [ in.at ];
		bool legacy = true;

		switch ( b ) {

			case 0xF0:
				prefixes |= LOCK;
				break;

			case 0xF2:
			case 0xF3:
				rep = b;
				break;

			case 0x2E:
				out.segment = CS;
				break;

			case 0x36:
				out.segment = SS;
				break;

			case 0x3E:
				out.segment = DS;
				notrack = true;
				break;

			case 0x26:
				out.segment = ES;
				break;

			case 0x64:
				out.segment = FS;
				break;

			case 0x65:
				out.segment = GS;
				break;

			case 0x66:
				opsize = true;
				break;

			case 0x67:
				addrsize = true;
				break;

			default:
				legacy = false;
				break;

		}

		if ( legacy ) {

			in.at++;
			rex = 0;

			continue;

		}

		if ( long_mode && ( b & 0xF0 ) == 0x40 ) {

			rex = b;
			in.at++;

			continue;

		}

		break;

	}

	if ( long_mode && ( out.segment == CS || out.segment == SS || out.segment == DS || out.segment == ES ) )
		out.segment = 0;

	s.rex_present = rex != 0;
	s.rex_w = ( rex & 8 ) != 0;
	s.rex_r = ( rex & 4 ) != 0;
	s.rex_x = ( rex & 2 ) != 0;
	s.rex_b = ( rex & 1 ) != 0;

	uint8_t first = in.byte ( );
	int32_t map = 0;
	uint8_t opcode = first;
	uint8_t pp = 0;

	if ( first == 0x0F ) {

		opcode = in.byte ( );
		map = 1;

		if ( opcode == 0x38 || opcode == 0x3A ) {

			map = opcode == 0x38 ? 2 : 3;
			opcode = in.byte ( );

		}

	} else if ( ( first == 0xC4 || first == 0xC5 || first == 0x62 ) && in.at < in.limit && ( long_mode || ( code [ in.at ] & 0xC0 ) == 0xC0 ) ) {

		if ( rex || opsize || rep || ( prefixes & LOCK ) )
			return false;

		uint8_t one_vex = in.byte ( );

		s.vex = true;
		s.rex_present = true;
		s.width = 16;

		if ( first == 0x62 ) {

			uint8_t two_vex = in.byte ( );
			uint8_t three_vex = in.byte ( );

			if ( ( one_vex & 0x08 ) || !( two_vex & 0x04 ) )
				return false;

			s.evex = true;
			s.rex_r = long_mode && !( one_vex & 0x80 );
			s.rex_x = long_mode && !( one_vex & 0x40 );
			s.rex_b = long_mode && !( one_vex & 0x20 );
			s.rex_r2 = long_mode && !( one_vex & 0x10 );
			map = one_vex & 7;
			s.rex_w = ( two_vex & 0x80 ) != 0;
			s.vvvv = static_cast < uint8_t > ( ( ( ~two_vex >> 3 ) & 15 ) | ( long_mode && !( three_vex & 0x08 ) ? 16 : 0 ) );
			pp = two_vex & 3;
			out.zeroing = ( three_vex & 0x80 ) != 0;
			out.mask = three_vex & 7;
			s.broadcast = ( three_vex & 0x10 ) != 0;
			s.width = static_cast < uint8_t > ( 16 << ( ( three_vex >> 5 ) & 3 ) );
			prefixes |= EVEX;

			if ( map == 0 || map == 4 || map == 7 )
				return false;

		} else if ( first == 0xC5 ) {

			s.rex_r = long_mode && !( one_vex & 0x80 );
			s.vvvv = static_cast < uint8_t > ( ( ~one_vex >> 3 ) & 15 );
			s.width = static_cast < uint8_t > ( ( one_vex & 4 ) ? 32 : 16 );
			pp = one_vex & 3;
			map = 1;

		} else {

			uint8_t two_vex = in.byte ( );

			s.rex_r = long_mode && !( one_vex & 0x80 );
			s.rex_x = long_mode && !( one_vex & 0x40 );
			s.rex_b = long_mode && !( one_vex & 0x20 );
			map = one_vex & 31;
			s.rex_w = ( two_vex & 0x80 ) != 0;
			s.vvvv = static_cast < uint8_t > ( ( ~two_vex >> 3 ) & 15 );
			s.width = static_cast < uint8_t > ( ( two_vex & 4 ) ? 32 : 16 );
			pp = two_vex & 3;

			if ( map < 1 || map > 3 )
				return false;

		}

		if ( !long_mode )
			s.vvvv &= 7;

		opcode = in.byte ( );
		prefixes |= VEX;

		if ( s.width >= 32 )
			prefixes |= VEX_L;

	}

	if ( in.failed )
		return false;

	if ( s.evex || ( s.vex && !( map == 1 && opcode == 0x77 ) ) )
		return decode_vector ( in, s, out, map, opcode, pp, addrsize, prefixes );

	form f;
	uint8_t mandatory = 0;
	bool handled = false;

	s.opcode = opcode;

	if ( handled ) {

	} else if ( map == 0 )
		f = one_byte [ opcode ];
	else {

		uint8_t slot = 0;

		if ( s.vex )
			slot = pp;
		else if ( rep == 0xF2 )
			slot = 3;
		else if ( rep == 0xF3 )
			slot = 2;
		else if ( opsize )
			slot = 1;

		uint16_t index = extended [ map - 1 ] [ opcode ] [ slot ];

		if ( index ) {

			mandatory = slot;

		} else if ( !s.vex && slot != 0 ) {

			if ( slot >= 2 && opsize && extended [ map - 1 ] [ opcode ] [ 1 ] ) {

				index = extended [ map - 1 ] [ opcode ] [ 1 ];
				mandatory = 1;

			} else
				index = extended [ map - 1 ] [ opcode ] [ 0 ];

		}

		bool usable = index != 0;

		if ( usable ) {

			f = pool [ index ];

			if ( s.vex && ( f.flags & NO_VEX ) )
				usable = false;

			if ( !s.vex && ( f.flags & VEX_ONLY ) )
				usable = false;

		}

		if ( !usable )
			return false;

		if ( s.vex && !s.evex && map == 1 && opcode == 0x77 ) {

			f.name = s.width >= 32 ? mnemonic::vzeroall : mnemonic::vzeroupper;
			f.flags = 0;

		}

	}

	bool distinct = map != 0 && !s.vex && mandatory && extended [ map - 1 ] [ opcode ] [ mandatory ] != extended [ map - 1 ] [ opcode ] [ 0 ];
	bool consumed_66 = mandatory == 1 && distinct;
	bool consumed_rep = ( mandatory == 2 || mandatory == 3 ) && distinct;

	if ( map == 0 && opcode == 0x90 ) {

		if ( rep == 0xF3 ) {

			f.name = mnemonic::pause;
			consumed_rep = true;

		} else if ( s.rex_b ) {

			f.name = mnemonic::xchg;
			f.ops [ 0 ] = R_AX;
			f.ops [ 1 ] = ZV;

		}

	}

	if ( long_mode && ( f.flags & I64 ) )
		return false;

	if ( !long_mode && ( f.flags & O64 ) )
		return false;

	if ( map == 0 && first == 0x63 && long_mode ) {

		f.name = mnemonic::movsxd;
		f.ops [ 0 ] = GV;
		f.ops [ 1 ] = ED;

	}

	bool opsize_active = opsize && !consumed_66;

	if ( long_mode ) {

		if ( s.rex_w && !s.vex )
			s.operand_size = 8;
		else if ( f.flags & F64 )
			s.operand_size = 8;
		else if ( f.flags & D64 )
			s.operand_size = opsize_active ? 2 : 8;
		else
			s.operand_size = opsize_active ? 2 : 4;

		if ( s.vex && s.rex_w )
			s.operand_size = 8;

		s.address_size = addrsize ? 4 : 8;

	} else {

		s.operand_size = opsize_active ? 2 : 4;
		s.address_size = addrsize ? 2 : 4;

	}

	if ( f.flags & MODRM ) {

		uint8_t modrm = in.byte ( );

		s.has_modrm = true;
		s.mod = modrm >> 6;
		s.reg = ( modrm >> 3 ) & 7;
		s.rm = modrm & 7;

		if ( f.flags & REG_ONLY )
			s.mod = 3;

		if ( s.width > 64 )
			return false;

		if ( s.mod != 3 ) {

			if ( !read_memory ( in, s, out ) )
				return false;

			s.memory_ready = true;

		}

	}

	if ( ( f.flags & MODRM ) && !refine ( f, s, map, opcode, mandatory ) )
		return false;

	if ( !( f.flags & MODRM ) )
		s.rm = static_cast < uint8_t > ( opcode & 7 );

	if ( in.failed )
		return false;

	if ( f.flags & X87 ) {

		int32_t index = opcode - 0xD8;

		f = s.mod == 3 ? x87_register [ index ] [ ( s.reg << 3 ) | s.rm ] : x87_memory [ index ] [ s.reg ];

		if ( f.name == mnemonic::invalid )
			return false;

		f.flags |= MODRM;

	} else if ( f.group ) {

		out.address = address;

		if ( !resolve_group ( f, s, mandatory, out ) )
			return false;

		if ( f.flags & D64 ) {

			if ( long_mode && !s.rex_w )
				s.operand_size = opsize_active ? 2 : 8;

		}

		if ( f.flags & F64 ) {

			if ( long_mode )
				s.operand_size = 8;

		}

		switch ( f.name ) {

			case mnemonic::endbr64:
			case mnemonic::endbr32:
			case mnemonic::rdsspd:
			case mnemonic::rdsspq:
			case mnemonic::rdpid:
			case mnemonic::rdfsbase:
			case mnemonic::rdgsbase:
			case mnemonic::wrfsbase:
			case mnemonic::wrgsbase:
			case mnemonic::incsspd:
			case mnemonic::incsspq:
				consumed_rep = true;
				break;

			default:
				break;

		}

	}

	if ( s.ksize )
		s.operand_size = s.ksize;

	if ( s.prefix_used )
		consumed_rep = true;

	if ( f.name == mnemonic::invalid )
		return false;

	if ( f.flags & W_NAME ) {

		if ( s.rex_w && ( s.long_mode || s.vex ) )
			f.name = offset ( f.name, 1 );

	}

	if ( f.flags & SIZE_NAME ) {

		uint8_t size = s.operand_size;
		mnemonic name = f.name;

		if ( name == mnemonic::pusha || name == mnemonic::popa )
			f.name = offset ( name, size == 2 ? 0 : 1 );
		else if ( name == mnemonic::pushf || name == mnemonic::popf )
			f.name = offset ( name, size == 2 ? 0 : ( long_mode ? 2 : 1 ) );
		else if ( name == mnemonic::insw || name == mnemonic::outsw )
			f.name = offset ( name, size == 2 ? 0 : 1 );
		else
			f.name = offset ( name, size == 2 ? 0 : ( size == 4 ? 1 : 2 ) );

	}

	if ( s.rex_w && long_mode && !s.vex ) {

		switch ( f.name ) {

			case mnemonic::fxsave:
				f.name = mnemonic::fxsave64;
				break;

			case mnemonic::fxrstor:
				f.name = mnemonic::fxrstor64;
				break;

			case mnemonic::xsave:
				f.name = mnemonic::xsave64;
				break;

			case mnemonic::xrstor:
				f.name = mnemonic::xrstor64;
				break;

			case mnemonic::xsaveopt:
				f.name = mnemonic::xsaveopt64;
				break;

			case mnemonic::xsavec:
				f.name = mnemonic::xsavec64;
				break;

			case mnemonic::xsaves:
				f.name = mnemonic::xsaves64;
				break;

			case mnemonic::xrstors:
				f.name = mnemonic::xrstors64;
				break;

			case mnemonic::retf:
				f.name = mnemonic::retfq;
				break;

			case mnemonic::sysret:
				f.name = mnemonic::sysretq;
				break;

			case mnemonic::sysexit:
				f.name = mnemonic::sysexitq;
				break;

			default:
				break;

		}

	}

	if ( f.name == mnemonic::jcxz )
		f.name = s.address_size == 2 ? mnemonic::jcxz : ( s.address_size == 4 ? mnemonic::jecxz : mnemonic::jrcxz );

	bool uses_h = false;

	for ( int32_t i = 0; i < 4; i++ ) {

		if ( f.ops [ i ] == HX || f.ops [ i ] == HO || f.ops [ i ] == HY || f.ops [ i ] == BY || f.ops [ i ] == KV )
			uses_h = true;

	}

	bool insert_h = s.vex && ( ( f.flags & NDS ) || ( ( f.flags & NDS_REG ) && s.mod == 3 ) );

	if ( s.vex && !insert_h && !uses_h && s.vvvv != 0 )
		return false;

	int32_t count = 0;

	for ( int32_t i = 0; i < 4 && f.ops [ i ] != NO; i++ ) {

		operand item;

		if ( !build_operand ( f.ops [ i ], in, s, out, item ) )
			return false;

		out.operands [ count++ ] = item;

		if ( i == 0 && insert_h ) {

			operand extra;
			int32_t width = item.type == kind::REG && is_vector ( item.reg ) ? register_size ( item.reg ) : s.width;

			extra.type = kind::REG;
			extra.reg = vector ( s.vvvv, width );
			extra.size = register_size ( extra.reg );
			out.operands [ count++ ] = extra;

		}

		if ( count >= 4 )
			break;

	}

	if ( in.failed )
		return false;

	if ( map == 1 && opcode == 0x0F && !s.vex ) {

		if ( !out.immediate_size ) {

			out.immediate_offset = static_cast < uint8_t > ( in.at );
			out.immediate_size = 1;

		}

		f.name = amd_3dnow ( in.byte ( ) );

		if ( f.name == mnemonic::invalid )
			return false;

	}

	if ( map == 0 && ( opcode == 0x86 || opcode == 0x87 ) && s.mod == 3 && count == 2 ) {

		operand held = out.operands [ 0 ];

		out.operands [ 0 ] = out.operands [ 1 ];
		out.operands [ 1 ] = held;

	}

	if ( map == 0 && opcode == 0x8C && count && out.operands [ 0 ].type == kind::MEM )
		out.operands [ 0 ].size = 2;

	if ( map == 0 && opcode == 0x8E && count > 1 && out.operands [ 1 ].type == kind::REG ) {

		out.operands [ 1 ].reg = resize_register ( out.operands [ 1 ].reg, s.operand_size );
		out.operands [ 1 ].size = s.operand_size;

	}

	out.length = static_cast < uint8_t > ( in.at );
	out.count = static_cast < uint8_t > ( count );
	out.name = f.name;
	out.operand_size = s.operand_size;

	for ( int32_t i = 0; i < count; i++ ) {

		operand& item = out.operands [ i ];

		if ( item.type == kind::REL )
			item.value = static_cast < int64_t > ( long_mode ? address + out.length + static_cast < uint64_t > ( item.value ) : static_cast < uint32_t > ( address + out.length + static_cast < uint64_t > ( item.value ) ) );

		if ( item.type == kind::MEM && item.base == NONE && item.index == NONE && s.address_size == 4 )
			item.value = static_cast < int64_t > ( static_cast < uint32_t > ( item.value ) );

		item.access = READ;

	}

	if ( count > 0 && !( f.flags & NOWRITE ) )
		out.operands [ 0 ].access = static_cast < uint8_t > ( writes_only ( f.name ) ? WRITE : READ | WRITE );

	if ( s.rex_w )
		prefixes |= REX_W;

	if ( rex )
		prefixes |= HAS_REX;

	if ( opsize && !consumed_66 && !s.prefix_used )
		prefixes |= OPSIZE;

	if ( addrsize )
		prefixes |= ADDRSIZE;

	if ( rep && !consumed_rep && !s.vex )
		prefixes |= rep == 0xF3 ? REP : REPNE;

	if ( notrack && ( f.name == mnemonic::jmp || f.name == mnemonic::call ) && count && out.operands [ 0 ].type != kind::REL )
		prefixes |= NOTRACK;

	out.prefixes = prefixes;

	int32_t value = static_cast < int32_t > ( f.name );

	if ( value >= static_cast < int32_t > ( mnemonic::jo ) && value <= static_cast < int32_t > ( mnemonic::jg ) ) {

		out.control = flow::BRANCH;
		out.condition = static_cast < uint8_t > ( value - static_cast < int32_t > ( mnemonic::jo ) );

	} else if ( value >= static_cast < int32_t > ( mnemonic::seto ) && value <= static_cast < int32_t > ( mnemonic::setg ) )
		out.condition = static_cast < uint8_t > ( value - static_cast < int32_t > ( mnemonic::seto ) );
	else if ( value >= static_cast < int32_t > ( mnemonic::cmovo ) && value <= static_cast < int32_t > ( mnemonic::cmovg ) )
		out.condition = static_cast < uint8_t > ( value - static_cast < int32_t > ( mnemonic::cmovo ) );

	switch ( f.name ) {

		case mnemonic::call:
		case mnemonic::callf:
			out.control = flow::CALL;
			break;

		case mnemonic::jmp:
		case mnemonic::jmpf:
			out.control = flow::JUMP;
			break;

		case mnemonic::jcxz:
		case mnemonic::jecxz:
		case mnemonic::jrcxz:
		case mnemonic::loop:
		case mnemonic::loope:
		case mnemonic::loopne:
		case mnemonic::xbegin:
			out.control = flow::BRANCH;
			break;

		case mnemonic::ret:
		case mnemonic::retf:
		case mnemonic::iret:
		case mnemonic::iretd:
		case mnemonic::iretq:
		case mnemonic::sysret:
		case mnemonic::sysexit:
		case mnemonic::retfq:
		case mnemonic::sysretq:
		case mnemonic::sysexitq:
			out.control = flow::RETURN;
			break;

		case mnemonic::int3:
		case mnemonic::hlt:
		case mnemonic::ud0:
		case mnemonic::ud1:
		case mnemonic::ud2:
			out.control = flow::STOP;
			break;

		case mnemonic::int_:

			if ( count && ( out.operands [ 0 ].value == 0x29 || out.operands [ 0 ].value == 0x2C ) )
				out.control = flow::STOP;

			break;

		default:
			break;

	}

	return true;

}