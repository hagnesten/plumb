#pragma once

#define PLUMB_MNEMONICS( X ) \
	X ( invalid ) \
	X ( aaa ) X ( aad ) X ( aam ) X ( aas ) X ( adc ) X ( adcx ) X ( add ) X ( adox ) X ( and_ ) X ( andn ) X ( arpl ) \
	X ( bextr ) X ( blsi ) X ( blsmsk ) X ( blsr ) X ( bound ) X ( bsf ) X ( bsr ) X ( bswap ) X ( bt ) X ( btc ) X ( btr ) X ( bts ) X ( bzhi ) \
	X ( call ) X ( callf ) X ( cbw ) X ( cwde ) X ( cdqe ) X ( cwd ) X ( cdq ) X ( cqo ) X ( clac ) X ( clc ) X ( cld ) X ( clflush ) X ( clflushopt ) X ( clwb ) X ( cli ) X ( clts ) X ( cmc ) \
	X ( cmpxchg ) X ( cmpxchg8b ) X ( cmpxchg16b ) X ( cmp ) X ( cpuid ) X ( crc32 ) \
	X ( daa ) X ( das ) X ( dec ) X ( div ) X ( emms ) X ( endbr32 ) X ( endbr64 ) X ( enter ) X ( fwait ) X ( getsec ) X ( hlt ) \
	X ( idiv ) X ( imul ) X ( in ) X ( inc ) X ( incsspd ) X ( incsspq ) X ( int_ ) X ( int1 ) X ( int3 ) X ( into ) X ( invd ) X ( invlpg ) X ( invpcid ) X ( iret ) X ( iretd ) X ( iretq ) \
	X ( jo ) X ( jno ) X ( jb ) X ( jae ) X ( je ) X ( jne ) X ( jbe ) X ( ja ) X ( js ) X ( jns ) X ( jp ) X ( jnp ) X ( jl ) X ( jge ) X ( jle ) X ( jg ) \
	X ( jcxz ) X ( jecxz ) X ( jrcxz ) X ( jmp ) X ( jmpf ) \
	X ( lahf ) X ( lar ) X ( lds ) X ( lea ) X ( leave ) X ( les ) X ( lfence ) X ( lfs ) X ( lgdt ) X ( lgs ) X ( lidt ) X ( lldt ) X ( lmsw ) \
	X ( loop ) X ( loope ) X ( loopne ) X ( lsl ) X ( lss ) X ( ltr ) X ( lzcnt ) \
	X ( mfence ) X ( monitor ) X ( mov ) X ( movbe ) X ( movsx ) X ( movsxd ) X ( movzx ) X ( mul ) X ( mulx ) X ( mwait ) \
	X ( neg ) X ( nop ) X ( not_ ) X ( or_ ) X ( out ) X ( pause ) X ( pdep ) X ( pext ) X ( pop ) X ( popa ) X ( popad ) X ( popcnt ) X ( popf ) X ( popfd ) X ( popfq ) \
	X ( prefetch ) X ( prefetchw ) X ( prefetchnta ) X ( prefetcht0 ) X ( prefetcht1 ) X ( prefetcht2 ) \
	X ( push ) X ( pusha ) X ( pushad ) X ( pushf ) X ( pushfd ) X ( pushfq ) \
	X ( rcl ) X ( rcr ) X ( rdfsbase ) X ( rdgsbase ) X ( rdmsr ) X ( rdpid ) X ( rdpkru ) X ( rdpmc ) X ( rdrand ) X ( rdseed ) X ( rdsspd ) X ( rdsspq ) X ( rdtsc ) X ( rdtscp ) \
	X ( ret ) X ( retf ) X ( rol ) X ( ror ) X ( rorx ) X ( rsm ) X ( sahf ) X ( salc ) X ( sar ) X ( sarx ) X ( sbb ) \
	X ( seto ) X ( setno ) X ( setb ) X ( setae ) X ( sete ) X ( setne ) X ( setbe ) X ( seta ) X ( sets ) X ( setns ) X ( setp ) X ( setnp ) X ( setl ) X ( setge ) X ( setle ) X ( setg ) \
	X ( sfence ) X ( sgdt ) X ( shl ) X ( shld ) X ( shlx ) X ( shr ) X ( shrd ) X ( shrx ) X ( sidt ) X ( sldt ) X ( smsw ) X ( stac ) X ( stc ) X ( std ) X ( sti ) X ( str ) X ( sub ) \
	X ( swapgs ) X ( syscall ) X ( sysenter ) X ( sysexit ) X ( sysret ) X ( test ) X ( tzcnt ) X ( ud0 ) X ( ud1 ) X ( ud2 ) X ( verr ) X ( verw ) \
	X ( vmcall ) X ( vmlaunch ) X ( vmresume ) X ( vmxoff ) X ( vmread ) X ( vmwrite ) X ( wbinvd ) X ( wrfsbase ) X ( wrgsbase ) X ( wrmsr ) X ( wrpkru ) \
	X ( xabort ) X ( xadd ) X ( xbegin ) X ( xchg ) X ( xend ) X ( xgetbv ) X ( xlatb ) X ( xor_ ) X ( xrstor ) X ( xsave ) X ( xsaveopt ) X ( xsetbv ) X ( xtest ) \
	X ( fxrstor ) X ( fxsave ) X ( ldmxcsr ) X ( stmxcsr ) X ( xrstors ) X ( xsavec ) X ( xsaves ) \
	X ( cmovo ) X ( cmovno ) X ( cmovb ) X ( cmovae ) X ( cmove ) X ( cmovne ) X ( cmovbe ) X ( cmova ) X ( cmovs ) X ( cmovns ) X ( cmovp ) X ( cmovnp ) X ( cmovl ) X ( cmovge ) X ( cmovle ) X ( cmovg ) \
	X ( movsb ) X ( movsw ) X ( movsd_ ) X ( movsq ) X ( cmpsb ) X ( cmpsw ) X ( cmpsd_ ) X ( cmpsq ) X ( stosb ) X ( stosw ) X ( stosd ) X ( stosq ) \
	X ( lodsb ) X ( lodsw ) X ( lodsd ) X ( lodsq ) X ( scasb ) X ( scasw ) X ( scasd ) X ( scasq ) X ( insb ) X ( insw ) X ( insd ) X ( outsb ) X ( outsw ) X ( outsd ) \
	X ( f2xm1 ) X ( fabs ) X ( fadd ) X ( faddp ) X ( fbld ) X ( fbstp ) X ( fchs ) X ( fcmovb ) X ( fcmovbe ) X ( fcmove ) X ( fcmovnb ) X ( fcmovnbe ) X ( fcmovne ) X ( fcmovnu ) X ( fcmovu ) \
	X ( fcom ) X ( fcomi ) X ( fcomip ) X ( fcomp ) X ( fcompp ) X ( fcos ) X ( fdecstp ) X ( fdiv ) X ( fdivp ) X ( fdivr ) X ( fdivrp ) X ( ffree ) X ( ffreep ) \
	X ( fiadd ) X ( ficom ) X ( ficomp ) X ( fidiv ) X ( fidivr ) X ( fild ) X ( fimul ) X ( fincstp ) X ( fist ) X ( fistp ) X ( fisttp ) X ( fisub ) X ( fisubr ) \
	X ( fld ) X ( fld1 ) X ( fldcw ) X ( fldenv ) X ( fldl2e ) X ( fldl2t ) X ( fldlg2 ) X ( fldln2 ) X ( fldpi ) X ( fldz ) X ( fmul ) X ( fmulp ) \
	X ( fnclex ) X ( fninit ) X ( fnop ) X ( fnsave ) X ( fnstcw ) X ( fnstenv ) X ( fnstsw ) X ( fpatan ) X ( fprem ) X ( fprem1 ) X ( fptan ) X ( frndint ) X ( frstor ) \
	X ( fscale ) X ( fsin ) X ( fsincos ) X ( fsqrt ) X ( fst ) X ( fstp ) X ( fsub ) X ( fsubp ) X ( fsubr ) X ( fsubrp ) X ( ftst ) X ( fucom ) X ( fucomi ) X ( fucomip ) X ( fucomp ) X ( fucompp ) \
	X ( fxam ) X ( fxch ) X ( fxtract ) X ( fyl2x ) X ( fyl2xp1 ) \
	X ( addpd ) X ( addps ) X ( addsd ) X ( addss ) X ( addsubpd ) X ( addsubps ) X ( aesdec ) X ( aesdeclast ) X ( aesenc ) X ( aesenclast ) X ( aesimc ) X ( aeskeygenassist ) \
	X ( andnpd ) X ( andnps ) X ( andpd ) X ( andps ) X ( blendpd ) X ( blendps ) X ( blendvpd ) X ( blendvps ) \
	X ( cmppd ) X ( cmpps ) X ( cmpsd ) X ( cmpss ) X ( comisd ) X ( comiss ) \
	X ( cvtdq2pd ) X ( cvtdq2ps ) X ( cvtpd2dq ) X ( cvtpd2pi ) X ( cvtpd2ps ) X ( cvtpi2pd ) X ( cvtpi2ps ) X ( cvtps2dq ) X ( cvtps2pd ) X ( cvtps2pi ) \
	X ( cvtsd2si ) X ( cvtsd2ss ) X ( cvtsi2sd ) X ( cvtsi2ss ) X ( cvtss2sd ) X ( cvtss2si ) X ( cvttpd2dq ) X ( cvttpd2pi ) X ( cvttps2dq ) X ( cvttps2pi ) X ( cvttsd2si ) X ( cvttss2si ) \
	X ( divpd ) X ( divps ) X ( divsd ) X ( divss ) X ( dppd ) X ( dpps ) X ( extractps ) X ( haddpd ) X ( haddps ) X ( hsubpd ) X ( hsubps ) X ( insertps ) X ( lddqu ) \
	X ( maskmovdqu ) X ( maskmovq ) X ( maxpd ) X ( maxps ) X ( maxsd ) X ( maxss ) X ( minpd ) X ( minps ) X ( minsd ) X ( minss ) \
	X ( movapd ) X ( movaps ) X ( movd ) X ( movq ) X ( movddup ) X ( movdq2q ) X ( movdqa ) X ( movdqu ) X ( movhlps ) X ( movhpd ) X ( movhps ) X ( movlhps ) X ( movlpd ) X ( movlps ) \
	X ( movmskpd ) X ( movmskps ) X ( movntdq ) X ( movntdqa ) X ( movnti ) X ( movntpd ) X ( movntps ) X ( movntq ) X ( movq2dq ) X ( movsd ) X ( movshdup ) X ( movsldup ) X ( movss ) X ( movupd ) X ( movups ) \
	X ( mpsadbw ) X ( mulpd ) X ( mulps ) X ( mulsd ) X ( mulss ) X ( orpd ) X ( orps ) \
	X ( pabsb ) X ( pabsd ) X ( pabsw ) X ( packssdw ) X ( packsswb ) X ( packusdw ) X ( packuswb ) X ( paddb ) X ( paddd ) X ( paddq ) X ( paddsb ) X ( paddsw ) X ( paddusb ) X ( paddusw ) X ( paddw ) \
	X ( palignr ) X ( pand ) X ( pandn ) X ( pavgb ) X ( pavgw ) X ( pblendvb ) X ( pblendw ) X ( pclmulqdq ) X ( pcmpeqb ) X ( pcmpeqd ) X ( pcmpeqq ) X ( pcmpeqw ) \
	X ( pcmpestri ) X ( pcmpestrm ) X ( pcmpgtb ) X ( pcmpgtd ) X ( pcmpgtq ) X ( pcmpgtw ) X ( pcmpistri ) X ( pcmpistrm ) \
	X ( pextrb ) X ( pextrd ) X ( pextrq ) X ( pextrw ) X ( phaddd ) X ( phaddsw ) X ( phaddw ) X ( phminposuw ) X ( phsubd ) X ( phsubsw ) X ( phsubw ) \
	X ( pinsrb ) X ( pinsrd ) X ( pinsrq ) X ( pinsrw ) X ( pmaddubsw ) X ( pmaddwd ) X ( pmaxsb ) X ( pmaxsd ) X ( pmaxsw ) X ( pmaxub ) X ( pmaxud ) X ( pmaxuw ) \
	X ( pminsb ) X ( pminsd ) X ( pminsw ) X ( pminub ) X ( pminud ) X ( pminuw ) X ( pmovmskb ) \
	X ( pmovsxbd ) X ( pmovsxbq ) X ( pmovsxbw ) X ( pmovsxdq ) X ( pmovsxwd ) X ( pmovsxwq ) X ( pmovzxbd ) X ( pmovzxbq ) X ( pmovzxbw ) X ( pmovzxdq ) X ( pmovzxwd ) X ( pmovzxwq ) \
	X ( pmuldq ) X ( pmulhrsw ) X ( pmulhuw ) X ( pmulhw ) X ( pmulld ) X ( pmullw ) X ( pmuludq ) X ( por ) X ( psadbw ) X ( pshufb ) X ( pshufd ) X ( pshufhw ) X ( pshuflw ) X ( pshufw ) \
	X ( psignb ) X ( psignd ) X ( psignw ) X ( pslld ) X ( pslldq ) X ( psllq ) X ( psllw ) X ( psrad ) X ( psraw ) X ( psrld ) X ( psrldq ) X ( psrlq ) X ( psrlw ) \
	X ( psubb ) X ( psubd ) X ( psubq ) X ( psubsb ) X ( psubsw ) X ( psubusb ) X ( psubusw ) X ( psubw ) X ( ptest ) \
	X ( punpckhbw ) X ( punpckhdq ) X ( punpckhqdq ) X ( punpckhwd ) X ( punpcklbw ) X ( punpckldq ) X ( punpcklqdq ) X ( punpcklwd ) X ( pxor ) \
	X ( rcpps ) X ( rcpss ) X ( roundpd ) X ( roundps ) X ( roundsd ) X ( roundss ) X ( rsqrtps ) X ( rsqrtss ) X ( shufpd ) X ( shufps ) \
	X ( sqrtpd ) X ( sqrtps ) X ( sqrtsd ) X ( sqrtss ) X ( subpd ) X ( subps ) X ( subsd ) X ( subss ) X ( ucomisd ) X ( ucomiss ) \
	X ( unpckhpd ) X ( unpckhps ) X ( unpcklpd ) X ( unpcklps ) X ( xorpd ) X ( xorps ) \
	X ( vzeroupper ) X ( vzeroall ) X ( vbroadcastss ) X ( vbroadcastsd ) X ( vbroadcastf128 ) X ( vbroadcasti128 ) X ( vpbroadcastb ) X ( vpbroadcastw ) X ( vpbroadcastd ) X ( vpbroadcastq ) \
	X ( vpermilps ) X ( vpermilpd ) X ( vtestps ) X ( vtestpd ) X ( vcvtph2ps ) X ( vcvtps2ph ) X ( vpermps ) X ( vpermd ) X ( vpermq ) X ( vpermpd ) X ( vperm2f128 ) X ( vperm2i128 ) \
	X ( vinsertf128 ) X ( vextractf128 ) X ( vinserti128 ) X ( vextracti128 ) X ( vmaskmovps ) X ( vmaskmovpd ) X ( vpmaskmovd ) X ( vpmaskmovq ) \
	X ( vpsrlvd ) X ( vpsrlvq ) X ( vpsravd ) X ( vpsllvd ) X ( vpsllvq ) X ( vpblendd ) X ( vblendvps ) X ( vblendvpd ) X ( vpblendvb ) \
	X ( vfmaddsub132ps ) X ( vfmaddsub132pd ) X ( vfmsubadd132ps ) X ( vfmsubadd132pd ) X ( vfmadd132ps ) X ( vfmadd132pd ) X ( vfmadd132ss ) X ( vfmadd132sd ) \
	X ( vfmsub132ps ) X ( vfmsub132pd ) X ( vfmsub132ss ) X ( vfmsub132sd ) X ( vfnmadd132ps ) X ( vfnmadd132pd ) X ( vfnmadd132ss ) X ( vfnmadd132sd ) \
	X ( vfnmsub132ps ) X ( vfnmsub132pd ) X ( vfnmsub132ss ) X ( vfnmsub132sd ) \
	X ( vfmaddsub213ps ) X ( vfmaddsub213pd ) X ( vfmsubadd213ps ) X ( vfmsubadd213pd ) X ( vfmadd213ps ) X ( vfmadd213pd ) X ( vfmadd213ss ) X ( vfmadd213sd ) \
	X ( vfmsub213ps ) X ( vfmsub213pd ) X ( vfmsub213ss ) X ( vfmsub213sd ) X ( vfnmadd213ps ) X ( vfnmadd213pd ) X ( vfnmadd213ss ) X ( vfnmadd213sd ) \
	X ( vfnmsub213ps ) X ( vfnmsub213pd ) X ( vfnmsub213ss ) X ( vfnmsub213sd ) \
	X ( vfmaddsub231ps ) X ( vfmaddsub231pd ) X ( vfmsubadd231ps ) X ( vfmsubadd231pd ) X ( vfmadd231ps ) X ( vfmadd231pd ) X ( vfmadd231ss ) X ( vfmadd231sd ) \
	X ( vfmsub231ps ) X ( vfmsub231pd ) X ( vfmsub231ss ) X ( vfmsub231sd ) X ( vfnmadd231ps ) X ( vfnmadd231pd ) X ( vfnmadd231ss ) X ( vfnmadd231sd ) \
	X ( vfnmsub231ps ) X ( vfnmsub231pd ) X ( vfnmsub231ss ) X ( vfnmsub231sd ) \
	X ( vmovdqa32 ) X ( vmovdqa64 ) X ( vmovdqu8 ) X ( vmovdqu16 ) X ( vmovdqu32 ) X ( vmovdqu64 ) X ( vpandd ) X ( vpandq ) X ( vpandnd ) X ( vpandnq ) X ( vpord ) X ( vporq ) X ( vpxord ) X ( vpxorq ) \
	X ( vptestmb ) X ( vptestmw ) X ( vptestmd ) X ( vptestmq ) X ( vptestnmb ) X ( vptestnmw ) X ( vptestnmd ) X ( vptestnmq ) X ( vpternlogd ) X ( vpternlogq ) \
	X ( vpcmpb ) X ( vpcmpw ) X ( vpcmpd ) X ( vpcmpq ) X ( vpcmpub ) X ( vpcmpuw ) X ( vpcmpud ) X ( vpcmpuq ) X ( avx512 ) \
	X ( kmovb ) X ( kmovw ) X ( kmovd ) X ( kmovq ) X ( kortestb ) X ( kortestw ) X ( kortestd ) X ( kortestq ) X ( ktestb ) X ( ktestw ) X ( ktestd ) X ( ktestq ) \
	X ( kandb ) X ( kandw ) X ( kandd ) X ( kandq ) X ( kandnb ) X ( kandnw ) X ( kandnd ) X ( kandnq ) X ( knotb ) X ( knotw ) X ( knotd ) X ( knotq ) \
	X ( korb ) X ( korw ) X ( kord ) X ( korq ) X ( kxnorb ) X ( kxnorw ) X ( kxnord ) X ( kxnorq ) X ( kxorb ) X ( kxorw ) X ( kxord ) X ( kxorq ) \
	X ( kaddb ) X ( kaddw ) X ( kaddd ) X ( kaddq ) X ( kunpckbw ) X ( kunpckwd ) X ( kunpckdq ) \
	X ( femms ) X ( pi2fw ) X ( pi2fd ) X ( pf2iw ) X ( pf2id ) X ( pfnacc ) X ( pfpnacc ) X ( pfcmpge ) X ( pfmin ) X ( pfrcp ) X ( pfrsqrt ) X ( pfsub ) \
	X ( pfadd ) X ( pfcmpgt ) X ( pfmax ) X ( pfrcpit1 ) X ( pfrsqit1 ) X ( pfsubr ) X ( pfacc ) X ( pfcmpeq ) X ( pfmul ) X ( pfrcpit2 ) X ( pmulhrw ) X ( pswapd ) \
	X ( pavgusb ) X ( sha1nexte ) X ( sha1msg1 ) X ( sha1msg2 ) X ( sha256rnds2 ) X ( sha256msg1 ) X ( sha256msg2 ) X ( sha1rnds4 ) X ( gf2p8mulb ) X ( gf2p8affineqb ) X ( gf2p8affineinvqb ) X ( movdiri ) \
	X ( movdir64b ) X ( enqcmd ) X ( enqcmds ) X ( wrssd ) X ( wrssq ) X ( wrussd ) X ( wrussq ) X ( rstorssp ) X ( clrssbsy ) X ( setssbsy ) X ( saveprevssp ) X ( enclv ) \
	X ( encls ) X ( enclu ) X ( pconfig ) X ( pbndkb ) X ( wrmsrns ) X ( wrmsrlist ) X ( rdmsrlist ) X ( vmfunc ) X ( vmrun ) X ( vmmcall ) X ( vmload ) X ( vmsave ) \
	X ( stgi ) X ( clgi ) X ( skinit ) X ( invlpga ) X ( monitorx ) X ( mwaitx ) X ( clzero ) X ( rdpru ) X ( invlpgb ) X ( tlbsync ) X ( serialize ) X ( xsusldtrk ) \
	X ( xresldtrk ) X ( uiret ) X ( testui ) X ( clui ) X ( stui ) X ( rmpquery ) X ( rmpadjust ) X ( rmpupdate ) X ( psmash ) X ( pvalidate ) X ( tdcall ) X ( seamret ) \
	X ( seamops ) X ( seamcall ) X ( loadiwkey ) X ( encodekey128 ) X ( encodekey256 ) X ( aesenc128kl ) X ( aesdec128kl ) X ( aesenc256kl ) X ( aesdec256kl ) X ( aesencwide128kl ) X ( aesdecwide128kl ) X ( aesencwide256kl ) \
	X ( aesdecwide256kl ) X ( uwrmsr ) X ( urdmsr ) X ( lkgs ) X ( tpause ) X ( umwait ) X ( umonitor ) X ( ptwrite ) X ( invept ) X ( invvpid ) X ( aadd ) X ( aand ) \
	X ( aor ) X ( axor ) X ( vmptrld ) X ( vmptrst ) X ( vmclear ) X ( vmxon ) X ( wbnoinvd ) X ( prefetchwt1 ) X ( prefetchit0 ) X ( prefetchit1 ) X ( cldemote ) X ( extrq ) \
	X ( insertq ) X ( retfq ) X ( sysretq ) X ( sysexitq ) X ( fxsave64 ) X ( fxrstor64 ) X ( xsave64 ) X ( xrstor64 ) X ( xsaveopt64 ) X ( xsavec64 ) X ( xsaves64 ) X ( xrstors64 ) \
	X ( movntss ) X ( movntsd ) X ( senduipi ) X ( erets ) X ( eretu ) \
	X ( vcvtqq2ps ) X ( vprord ) X ( vprorq ) X ( vprold ) X ( vprolq ) X ( vpsraq ) X ( vcvttps2udq ) X ( vcvttpd2udq ) \
	X ( vcvttps2uqq ) X ( vcvttpd2uqq ) X ( vcvttss2usi ) X ( vcvttsd2usi ) X ( vcvtps2udq ) X ( vcvtpd2udq ) X ( vcvtps2uqq ) X ( vcvtpd2uqq ) \
	X ( vcvtss2usi ) X ( vcvtsd2usi ) X ( vcvttps2qq ) X ( vcvttpd2qq ) X ( vcvtudq2pd ) X ( vcvtuqq2pd ) X ( vcvtudq2ps ) X ( vcvtuqq2ps ) \
	X ( vcvtps2qq ) X ( vcvtpd2qq ) X ( vcvtusi2ss ) X ( vcvtusi2sd ) X ( vcvtqq2pd ) X ( vpsrlvw ) X ( vpmovuswb ) X ( vpsravw ) \
	X ( vpmovusdb ) X ( vpsllvw ) X ( vpmovusqb ) X ( vpmovusdw ) X ( vprorvd ) X ( vprorvq ) X ( vpmovusqw ) X ( vprolvd ) \
	X ( vprolvq ) X ( vpmovusqd ) X ( vbroadcastf32x2 ) X ( vbroadcastf32x4 ) X ( vbroadcastf64x2 ) X ( vbroadcastf32x8 ) X ( vbroadcastf64x4 ) X ( vpabsq ) \
	X ( vpmovswb ) X ( vpmovsdb ) X ( vpmovsqb ) X ( vpmovsdw ) X ( vpmovsqw ) X ( vpmovsqd ) X ( vpmovm2b ) X ( vpmovm2w ) \
	X ( vpmovb2m ) X ( vpmovw2m ) X ( vpbroadcastmb2q ) X ( vscalefps ) X ( vscalefpd ) X ( vscalefss ) X ( vscalefsd ) X ( vpmovwb ) \
	X ( vpmovdb ) X ( vpmovqb ) X ( vpmovdw ) X ( vpmovqw ) X ( vpmovqd ) X ( vpmovm2d ) X ( vpmovm2q ) X ( vpminsq ) \
	X ( vpmovd2m ) X ( vpmovq2m ) X ( vpbroadcastmw2d ) X ( vpminuq ) X ( vpmaxsq ) X ( vpmaxuq ) X ( vpmullq ) X ( vgetexpps ) \
	X ( vgetexppd ) X ( vgetexpss ) X ( vgetexpsd ) X ( vplzcntd ) X ( vplzcntq ) X ( vpsravq ) X ( vrcp14ps ) X ( vrcp14pd ) \
	X ( vrcp14ss ) X ( vrcp14sd ) X ( vrsqrt14ps ) X ( vrsqrt14pd ) X ( vrsqrt14ss ) X ( vrsqrt14sd ) X ( vpdpbusd ) X ( vpdpbusds ) \
	X ( vpdpwssd ) X ( vdpbf16ps ) X ( vp4dpwssd ) X ( vpdpwssds ) X ( vp4dpwssds ) X ( vpopcntb ) X ( vpopcntw ) X ( vpopcntd ) \
	X ( vpopcntq ) X ( vbroadcasti32x2 ) X ( vbroadcasti32x4 ) X ( vbroadcasti64x2 ) X ( vbroadcasti32x8 ) X ( vbroadcasti64x4 ) X ( vpexpandb ) X ( vpexpandw ) \
	X ( vpcompressb ) X ( vpcompressw ) X ( vpblendmd ) X ( vpblendmq ) X ( vblendmps ) X ( vblendmpd ) X ( vpblendmb ) X ( vpblendmw ) \
	X ( vp2intersectd ) X ( vp2intersectq ) X ( vpshldvw ) X ( vpshldvd ) X ( vpshldvq ) X ( vpshrdvw ) X ( vcvtneps2bf16 ) X ( vcvtne2ps2bf16 ) \
	X ( vpshrdvd ) X ( vpshrdvq ) X ( vpermi2b ) X ( vpermi2w ) X ( vpermi2d ) X ( vpermi2q ) X ( vpermi2ps ) X ( vpermi2pd ) \
	X ( vpermt2b ) X ( vpermt2w ) X ( vpermt2d ) X ( vpermt2q ) X ( vpermt2ps ) X ( vpermt2pd ) X ( vpmultishiftqb ) X ( vexpandps ) \
	X ( vexpandpd ) X ( vpexpandd ) X ( vpexpandq ) X ( vcompressps ) X ( vcompresspd ) X ( vpcompressd ) X ( vpcompressq ) X ( vpermb ) \
	X ( vpermw ) X ( vpshufbitqmb ) X ( vpgatherdd ) X ( vpgatherdq ) X ( vpgatherqd ) X ( vpgatherqq ) X ( vgatherdps ) X ( vgatherdpd ) \
	X ( vgatherqps ) X ( vgatherqpd ) X ( v4fmaddps ) X ( v4fmaddss ) X ( vpscatterdd ) X ( vpscatterdq ) X ( vpscatterqd ) X ( vpscatterqq ) \
	X ( vscatterdps ) X ( vscatterdpd ) X ( vscatterqps ) X ( vscatterqpd ) X ( v4fnmaddps ) X ( v4fnmaddss ) X ( vpmadd52luq ) X ( vpmadd52huq ) \
	X ( vpconflictd ) X ( vpconflictq ) X ( vexp2ps ) X ( vexp2pd ) X ( vrcp28ps ) X ( vrcp28pd ) X ( vrcp28ss ) X ( vrcp28sd ) \
	X ( vrsqrt28ps ) X ( vrsqrt28pd ) X ( vrsqrt28ss ) X ( vrsqrt28sd ) X ( vgf2p8mulb ) X ( valignd ) X ( valignq ) X ( vrndscaleph ) \
	X ( vrndscaleps ) X ( vrndscalepd ) X ( vrndscalesh ) X ( vrndscaless ) X ( vrndscalesd ) X ( vinsertf32x4 ) X ( vinsertf64x2 ) X ( vextractf32x4 ) \
	X ( vextractf64x2 ) X ( vinsertf32x8 ) X ( vinsertf64x4 ) X ( vextractf32x8 ) X ( vextractf64x4 ) X ( vshuff32x4 ) X ( vshuff64x2 ) X ( vgetmantph ) \
	X ( vgetmantps ) X ( vgetmantpd ) X ( vgetmantsh ) X ( vgetmantss ) X ( vgetmantsd ) X ( vinserti32x4 ) X ( vinserti64x2 ) X ( vextracti32x4 ) \
	X ( vextracti64x2 ) X ( vinserti32x8 ) X ( vinserti64x4 ) X ( vextracti32x8 ) X ( vextracti64x4 ) X ( vdbpsadbw ) X ( vshufi32x4 ) X ( vshufi64x2 ) \
	X ( vrangeps ) X ( vrangepd ) X ( vrangess ) X ( vrangesd ) X ( vfixupimmps ) X ( vfixupimmpd ) X ( vfixupimmss ) X ( vfixupimmsd ) \
	X ( vreduceph ) X ( vreduceps ) X ( vreducepd ) X ( vreducesh ) X ( vreducess ) X ( vreducesd ) X ( vfpclassph ) X ( vfpclassps ) \
	X ( vfpclasspd ) X ( vfpclasssh ) X ( vfpclassss ) X ( vfpclasssd ) X ( vpshldw ) X ( vpshldd ) X ( vpshldq ) X ( vpshrdw ) \
	X ( vpshrdd ) X ( vpshrdq ) X ( vcmpph ) X ( vcmpsh ) X ( vgf2p8affineqb ) X ( vgf2p8affineinvqb ) X ( vmovsh ) X ( vcvtss2sh ) \
	X ( vcvtps2phx ) X ( vcvtsi2sh ) X ( vcvttsh2si ) X ( vcvtsh2si ) X ( vucomish ) X ( vcomish ) X ( vsqrtph ) X ( vsqrtsh ) \
	X ( vaddph ) X ( vaddsh ) X ( vmulph ) X ( vmulsh ) X ( vcvtph2pd ) X ( vcvtpd2ph ) X ( vcvtsh2sd ) X ( vcvtsd2sh ) \
	X ( vcvtdq2ph ) X ( vcvtqq2ph ) X ( vcvtph2dq ) X ( vcvttph2dq ) X ( vsubph ) X ( vsubsh ) X ( vminph ) X ( vminsh ) \
	X ( vdivph ) X ( vdivsh ) X ( vmaxph ) X ( vmaxsh ) X ( vmovw ) X ( vcvttph2udq ) X ( vcvttph2uqq ) X ( vcvttsh2usi ) \
	X ( vcvtph2udq ) X ( vcvtph2uqq ) X ( vcvtsh2usi ) X ( vcvttph2qq ) X ( vcvtudq2ph ) X ( vcvtuqq2ph ) X ( vcvtph2qq ) X ( vcvtusi2sh ) \
	X ( vcvttph2uw ) X ( vcvttph2w ) X ( vcvtph2uw ) X ( vcvtph2w ) X ( vcvtw2ph ) X ( vcvtuw2ph ) X ( vcvtsh2ss ) X ( vcvtph2psx ) \
	X ( vscalefph ) X ( vscalefsh ) X ( vgetexpph ) X ( vgetexpsh ) X ( vrcpph ) X ( vrcpsh ) X ( vrsqrtph ) X ( vrsqrtsh ) \
	X ( vfmaddcph ) X ( vfcmaddcph ) X ( vfmaddcsh ) X ( vfcmaddcsh ) X ( vfmaddsub132ph ) X ( vfmsubadd132ph ) X ( vfmadd132ph ) X ( vfmadd132sh ) \
	X ( vfmsub132ph ) X ( vfmsub132sh ) X ( vfnmadd132ph ) X ( vfnmadd132sh ) X ( vfnmsub132ph ) X ( vfnmsub132sh ) X ( vfmaddsub213ph ) X ( vfmsubadd213ph ) \
	X ( vfmadd213ph ) X ( vfmadd213sh ) X ( vfmsub213ph ) X ( vfmsub213sh ) X ( vfnmadd213ph ) X ( vfnmadd213sh ) X ( vfnmsub213ph ) X ( vfnmsub213sh ) \
	X ( vfmaddsub231ph ) X ( vfmsubadd231ph ) X ( vfmadd231ph ) X ( vfmadd231sh ) X ( vfmsub231ph ) X ( vfmsub231sh ) X ( vfnmadd231ph ) X ( vfnmadd231sh ) \
	X ( vfnmsub231ph ) X ( vfnmsub231sh ) X ( vfmulcph ) X ( vfcmulcph ) X ( vfmulcsh ) X ( vfcmulcsh ) \
	X ( ldtilecfg ) X ( sttilecfg ) X ( vldmxcsr ) X ( vstmxcsr ) X ( tilerelease ) X ( tilezero ) X ( tileloaddt1 ) X ( tilestored ) \
	X ( tileloadd ) X ( vpdpbuud ) X ( vpdpbsud ) X ( vpdpbssd ) X ( vpdpbuuds ) X ( vpdpbsuds ) X ( vpdpbssds ) X ( tdpbf16ps ) \
	X ( tdpfp16ps ) X ( tdpbuud ) X ( tdpbusd ) X ( tdpbsud ) X ( tdpbssd ) X ( tcmmrlfp16ps ) X ( tcmmimfp16ps ) X ( vcvtneoph2ps ) \
	X ( vcvtneeph2ps ) X ( vcvtneebf162ps ) X ( vcvtneobf162ps ) X ( vbcstnesh2ps ) X ( vbcstnebf162ps ) X ( vsha512rnds2 ) X ( vsha512msg1 ) X ( vsha512msg2 ) \
	X ( vpdpwuud ) X ( vpdpwusd ) X ( vpdpwsud ) X ( vpdpwuuds ) X ( vpdpwusds ) X ( vpdpwsuds ) X ( vsm3msg1 ) X ( vsm3msg2 ) \
	X ( vsm4key4 ) X ( vsm4rnds4 ) X ( cmpoxadd ) X ( cmpnoxadd ) X ( cmpbxadd ) X ( cmpnbxadd ) X ( cmpzxadd ) X ( cmpnzxadd ) \
	X ( cmpbexadd ) X ( cmpnbexadd ) X ( cmpsxadd ) X ( cmpnsxadd ) X ( cmppxadd ) X ( cmpnpxadd ) X ( cmplxadd ) X ( cmpnlxadd ) \
	X ( cmplexadd ) X ( cmpnlexadd ) X ( kshiftrb ) X ( kshiftrw ) X ( kshiftrd ) X ( kshiftrq ) X ( kshiftlb ) X ( kshiftlw ) \
	X ( kshiftld ) X ( kshiftlq ) X ( vpermil2ps ) X ( vpermil2pd ) X ( vfmaddsubps ) X ( vfmaddsubpd ) X ( vfmsubaddps ) X ( vfmsubaddpd ) \
	X ( vfmaddps ) X ( vfmaddpd ) X ( vfmaddss ) X ( vfmaddsd ) X ( vfmsubps ) X ( vfmsubpd ) X ( vfmsubss ) X ( vfmsubsd ) \
	X ( vfnmaddps ) X ( vfnmaddpd ) X ( vfnmaddss ) X ( vfnmaddsd ) X ( vfnmsubps ) X ( vfnmsubpd ) X ( vfnmsubss ) X ( vfnmsubsd ) \
	X ( vsm3rnds2 )