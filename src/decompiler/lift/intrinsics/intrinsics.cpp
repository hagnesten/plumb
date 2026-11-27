#include "decompiler/lift/intrinsics/intrinsics.h"
#include "decompiler/builder/builder.h"

const wchar_t* const decompiler::intrinsics [ ] = {
	L"__debugbreak", L"memset", L"qmemcpy", L"_tzcnt", L"_lzcnt", L"__popcnt", L"_byteswap", L"_rotl", L"_rotr", L"__umulh", L"__mulh",
	L"sqrtf", L"sqrt", L"fminf", L"fmaxf", L"fmin", L"fmax", L"__rdtsc", L"__cpuid", L"_InterlockedExchangeAdd", L"_InterlockedCompareExchange",
	L"_InterlockedExchange", L"__overflow", L"__parity", L"__unordered", L"__readfsqword", L"__readgsqword", L"__fastfail", L"_bittest", L"__halt",
	L"_mm_and", L"_mm_or", L"_mm_xor", L"_mm_andnot", L"__shiftleft128", L"__shiftright128", L"__readeflags", L"_xgetbv", L"__syscall",
	L"NtCurrentTeb", L"__readgsbyte", L"__readgsword", L"__readgsdword", L"__readfsbyte", L"__readfsword", L"__readfsdword",
	L"_mm_storel_ps", L"_mm_storeh_ps", L"_mm_loadl_pi", L"_mm_loadh_pi", L"_mm_storel_pd", L"_mm_storeh_pd", L"_mm_loadl_pd", L"_mm_loadh_pd",
	L"_mm_xor_ps", L"_mm_xor_pd", L"_mm_xor_si128", L"_mm_and_ps", L"_mm_and_pd", L"_mm_or_ps", L"_mm_or_pd"
};

const wchar_t* const decompiler::compare_names [ 32 ] = {
	L"_mm_cmpeq_ps", L"_mm_cmplt_ps", L"_mm_cmple_ps", L"_mm_cmpunord_ps", L"_mm_cmpneq_ps", L"_mm_cmpnlt_ps", L"_mm_cmpnle_ps", L"_mm_cmpord_ps",
	L"_mm_cmpeq_ss", L"_mm_cmplt_ss", L"_mm_cmple_ss", L"_mm_cmpunord_ss", L"_mm_cmpneq_ss", L"_mm_cmpnlt_ss", L"_mm_cmpnle_ss", L"_mm_cmpord_ss",
	L"_mm_cmpeq_pd", L"_mm_cmplt_pd", L"_mm_cmple_pd", L"_mm_cmpunord_pd", L"_mm_cmpneq_pd", L"_mm_cmpnlt_pd", L"_mm_cmpnle_pd", L"_mm_cmpord_pd",
	L"_mm_cmpeq_sd", L"_mm_cmplt_sd", L"_mm_cmple_sd", L"_mm_cmpunord_sd", L"_mm_cmpneq_sd", L"_mm_cmpnlt_sd", L"_mm_cmpnle_sd", L"_mm_cmpord_sd"
};

const decompiler::packed_form decompiler::packed_forms [ ] = {
	{ mnemonic::addps, L"_mm_add_ps", nullptr, P_BINARY, true }, { mnemonic::addpd, L"_mm_add_pd", nullptr, P_BINARY, true },
	{ mnemonic::subps, L"_mm_sub_ps", nullptr, P_BINARY, true }, { mnemonic::subpd, L"_mm_sub_pd", nullptr, P_BINARY, true },
	{ mnemonic::mulps, L"_mm_mul_ps", nullptr, P_BINARY, true }, { mnemonic::mulpd, L"_mm_mul_pd", nullptr, P_BINARY, true },
	{ mnemonic::divps, L"_mm_div_ps", nullptr, P_BINARY, true }, { mnemonic::divpd, L"_mm_div_pd", nullptr, P_BINARY, true },
	{ mnemonic::minps, L"_mm_min_ps", nullptr, P_BINARY, true }, { mnemonic::minpd, L"_mm_min_pd", nullptr, P_BINARY, true },
	{ mnemonic::maxps, L"_mm_max_ps", nullptr, P_BINARY, true }, { mnemonic::maxpd, L"_mm_max_pd", nullptr, P_BINARY, true },
	{ mnemonic::andnps, L"_mm_andnot_ps", nullptr, P_BINARY, true }, { mnemonic::andnpd, L"_mm_andnot_pd", nullptr, P_BINARY, true },
	{ mnemonic::unpcklps, L"_mm_unpacklo_ps", nullptr, P_BINARY, true }, { mnemonic::unpckhps, L"_mm_unpackhi_ps", nullptr, P_BINARY, true },
	{ mnemonic::unpcklpd, L"_mm_unpacklo_pd", nullptr, P_BINARY, true }, { mnemonic::unpckhpd, L"_mm_unpackhi_pd", nullptr, P_BINARY, true },
	{ mnemonic::movlhps, L"_mm_movelh_ps", nullptr, P_BINARY, true }, { mnemonic::movhlps, L"_mm_movehl_ps", nullptr, P_BINARY, true },
	{ mnemonic::shufps, L"_mm_shuffle_ps", nullptr, P_BINARY_IMM, true }, { mnemonic::shufpd, L"_mm_shuffle_pd", nullptr, P_BINARY_IMM, true },
	{ mnemonic::sqrtps, L"_mm_sqrt_ps", nullptr, P_UNARY, true }, { mnemonic::sqrtpd, L"_mm_sqrt_pd", nullptr, P_UNARY, true },
	{ mnemonic::rcpps, L"_mm_rcp_ps", nullptr, P_UNARY, true }, { mnemonic::rsqrtps, L"_mm_rsqrt_ps", nullptr, P_UNARY, true },
	{ mnemonic::cvtdq2ps, L"_mm_cvtepi32_ps", nullptr, P_UNARY, true }, { mnemonic::cvtps2dq, L"_mm_cvtps_epi32", nullptr, P_UNARY, false },
	{ mnemonic::cvttps2dq, L"_mm_cvttps_epi32", nullptr, P_UNARY, false }, { mnemonic::cvtdq2pd, L"_mm_cvtepi32_pd", nullptr, P_UNARY, true },
	{ mnemonic::cvtps2pd, L"_mm_cvtps_pd", nullptr, P_UNARY, true }, { mnemonic::cvtpd2ps, L"_mm_cvtpd_ps", nullptr, P_UNARY, true },
	{ mnemonic::pand, L"_mm_and_si128", nullptr, P_BINARY, false }, { mnemonic::pandn, L"_mm_andnot_si128", nullptr, P_BINARY, false },
	{ mnemonic::por, L"_mm_or_si128", nullptr, P_BINARY, false },
	{ mnemonic::paddb, L"_mm_add_epi8", nullptr, P_BINARY, false }, { mnemonic::paddw, L"_mm_add_epi16", nullptr, P_BINARY, false },
	{ mnemonic::paddd, L"_mm_add_epi32", nullptr, P_BINARY, false }, { mnemonic::paddq, L"_mm_add_epi64", nullptr, P_BINARY, false },
	{ mnemonic::psubb, L"_mm_sub_epi8", nullptr, P_BINARY, false }, { mnemonic::psubw, L"_mm_sub_epi16", nullptr, P_BINARY, false },
	{ mnemonic::psubd, L"_mm_sub_epi32", nullptr, P_BINARY, false }, { mnemonic::psubq, L"_mm_sub_epi64", nullptr, P_BINARY, false },
	{ mnemonic::paddusb, L"_mm_adds_epu8", nullptr, P_BINARY, false }, { mnemonic::paddusw, L"_mm_adds_epu16", nullptr, P_BINARY, false },
	{ mnemonic::paddsb, L"_mm_adds_epi8", nullptr, P_BINARY, false }, { mnemonic::paddsw, L"_mm_adds_epi16", nullptr, P_BINARY, false },
	{ mnemonic::psubusb, L"_mm_subs_epu8", nullptr, P_BINARY, false }, { mnemonic::psubusw, L"_mm_subs_epu16", nullptr, P_BINARY, false },
	{ mnemonic::psubsb, L"_mm_subs_epi8", nullptr, P_BINARY, false }, { mnemonic::psubsw, L"_mm_subs_epi16", nullptr, P_BINARY, false },
	{ mnemonic::pmullw, L"_mm_mullo_epi16", nullptr, P_BINARY, false }, { mnemonic::pmulld, L"_mm_mullo_epi32", nullptr, P_BINARY, false },
	{ mnemonic::pmulhw, L"_mm_mulhi_epi16", nullptr, P_BINARY, false }, { mnemonic::pmulhuw, L"_mm_mulhi_epu16", nullptr, P_BINARY, false },
	{ mnemonic::pmuludq, L"_mm_mul_epu32", nullptr, P_BINARY, false }, { mnemonic::pmaddwd, L"_mm_madd_epi16", nullptr, P_BINARY, false },
	{ mnemonic::psadbw, L"_mm_sad_epu8", nullptr, P_BINARY, false }, { mnemonic::pavgb, L"_mm_avg_epu8", nullptr, P_BINARY, false },
	{ mnemonic::pavgw, L"_mm_avg_epu16", nullptr, P_BINARY, false },
	{ mnemonic::pminub, L"_mm_min_epu8", nullptr, P_BINARY, false }, { mnemonic::pmaxub, L"_mm_max_epu8", nullptr, P_BINARY, false },
	{ mnemonic::pminsw, L"_mm_min_epi16", nullptr, P_BINARY, false }, { mnemonic::pmaxsw, L"_mm_max_epi16", nullptr, P_BINARY, false },
	{ mnemonic::pminsd, L"_mm_min_epi32", nullptr, P_BINARY, false }, { mnemonic::pmaxsd, L"_mm_max_epi32", nullptr, P_BINARY, false },
	{ mnemonic::pminud, L"_mm_min_epu32", nullptr, P_BINARY, false }, { mnemonic::pmaxud, L"_mm_max_epu32", nullptr, P_BINARY, false },
	{ mnemonic::pcmpeqb, L"_mm_cmpeq_epi8", nullptr, P_BINARY, false }, { mnemonic::pcmpeqw, L"_mm_cmpeq_epi16", nullptr, P_BINARY, false },
	{ mnemonic::pcmpeqd, L"_mm_cmpeq_epi32", nullptr, P_BINARY, false }, { mnemonic::pcmpeqq, L"_mm_cmpeq_epi64", nullptr, P_BINARY, false },
	{ mnemonic::pcmpgtb, L"_mm_cmpgt_epi8", nullptr, P_BINARY, false }, { mnemonic::pcmpgtw, L"_mm_cmpgt_epi16", nullptr, P_BINARY, false },
	{ mnemonic::pcmpgtd, L"_mm_cmpgt_epi32", nullptr, P_BINARY, false }, { mnemonic::pcmpgtq, L"_mm_cmpgt_epi64", nullptr, P_BINARY, false },
	{ mnemonic::punpcklbw, L"_mm_unpacklo_epi8", nullptr, P_BINARY, false }, { mnemonic::punpcklwd, L"_mm_unpacklo_epi16", nullptr, P_BINARY, false },
	{ mnemonic::punpckldq, L"_mm_unpacklo_epi32", nullptr, P_BINARY, false }, { mnemonic::punpcklqdq, L"_mm_unpacklo_epi64", nullptr, P_BINARY, false },
	{ mnemonic::punpckhbw, L"_mm_unpackhi_epi8", nullptr, P_BINARY, false }, { mnemonic::punpckhwd, L"_mm_unpackhi_epi16", nullptr, P_BINARY, false },
	{ mnemonic::punpckhdq, L"_mm_unpackhi_epi32", nullptr, P_BINARY, false }, { mnemonic::punpckhqdq, L"_mm_unpackhi_epi64", nullptr, P_BINARY, false },
	{ mnemonic::packuswb, L"_mm_packus_epi16", nullptr, P_BINARY, false }, { mnemonic::packsswb, L"_mm_packs_epi16", nullptr, P_BINARY, false },
	{ mnemonic::packssdw, L"_mm_packs_epi32", nullptr, P_BINARY, false }, { mnemonic::packusdw, L"_mm_packus_epi32", nullptr, P_BINARY, false },
	{ mnemonic::pshufb, L"_mm_shuffle_epi8", nullptr, P_BINARY, false }, { mnemonic::palignr, L"_mm_alignr_epi8", nullptr, P_BINARY_IMM, false },
	{ mnemonic::pshufd, L"_mm_shuffle_epi32", nullptr, P_UNARY_IMM, false }, { mnemonic::pshuflw, L"_mm_shufflelo_epi16", nullptr, P_UNARY_IMM, false },
	{ mnemonic::pshufhw, L"_mm_shufflehi_epi16", nullptr, P_UNARY_IMM, false },
	{ mnemonic::psrlw, L"_mm_srl_epi16", L"_mm_srli_epi16", P_SHIFT, false }, { mnemonic::psrld, L"_mm_srl_epi32", L"_mm_srli_epi32", P_SHIFT, false },
	{ mnemonic::psrlq, L"_mm_srl_epi64", L"_mm_srli_epi64", P_SHIFT, false }, { mnemonic::psllw, L"_mm_sll_epi16", L"_mm_slli_epi16", P_SHIFT, false },
	{ mnemonic::pslld, L"_mm_sll_epi32", L"_mm_slli_epi32", P_SHIFT, false }, { mnemonic::psllq, L"_mm_sll_epi64", L"_mm_slli_epi64", P_SHIFT, false },
	{ mnemonic::psraw, L"_mm_sra_epi16", L"_mm_srai_epi16", P_SHIFT, false }, { mnemonic::psrad, L"_mm_sra_epi32", L"_mm_srai_epi32", P_SHIFT, false },
	{ mnemonic::psrldq, L"_mm_srli_si128", L"_mm_srli_si128", P_SHIFT, false }, { mnemonic::pslldq, L"_mm_slli_si128", L"_mm_slli_si128", P_SHIFT, false },
	{ mnemonic::pmovmskb, L"_mm_movemask_epi8", nullptr, P_TO_GPR, false }, { mnemonic::movmskps, L"_mm_movemask_ps", nullptr, P_TO_GPR, true },
	{ mnemonic::movmskpd, L"_mm_movemask_pd", nullptr, P_TO_GPR, true }, { mnemonic::pextrw, L"_mm_extract_epi16", nullptr, P_TO_GPR_IMM, false },
	{ mnemonic::pextrd, L"_mm_extract_epi32", nullptr, P_TO_GPR_IMM, false }, { mnemonic::pextrb, L"_mm_extract_epi8", nullptr, P_TO_GPR_IMM, false },
};

const int32_t decompiler::packed_forms_count = static_cast < int32_t > ( sizeof ( packed_forms ) / sizeof ( packed_forms [ 0 ] ) );

const wchar_t* decompiler::intrinsic_name ( int32_t id ) {

	if ( id >= compare_base && id < compare_base + 32 )
		return compare_names [ id - compare_base ];

	if ( id >= packed_base ) {

		int32_t index = ( id - packed_base ) / 2;

		if ( index < packed_forms_count )
			return ( ( id - packed_base ) & 1 ) && packed_forms [ index ].immediate ? packed_forms [ index ].immediate : packed_forms [ index ].text;

	}

	return id >= 0 && id < static_cast < int32_t > ( sizeof ( intrinsics ) / sizeof ( intrinsics [ 0 ] ) ) ? intrinsics [ id ] : L"__intrinsic";

}