// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include "Common/CommonTypes.h"
#include "Common/CPUDetect.h"

#include "Core/PowerPC/Jit64/Jit.h"
#include "Core/PowerPC/Jit64/JitRegCache.h"

using namespace Gen;

static const u64 GC_ALIGNED16(psSignBits[2]) = {0x8000000000000000ULL, 0x8000000000000000ULL};
static const u64 GC_ALIGNED16(psAbsMask[2])  = {0x7FFFFFFFFFFFFFFFULL, 0x7FFFFFFFFFFFFFFFULL};

void Jit64::ps_mr(UGeckoInstruction inst)
{
	INSTRUCTION_START
	JITDISABLE(bJITPairedOff);
	FALLBACK_IF(inst.Rc);

	int d = inst.FD;
	int b = inst.FB;
	if (d == b)
		return;

	fpr.BindToRegister(d, false);
	MOVAPD(fpr.RX(d), fpr.R(b));
}

void Jit64::ps_sign(UGeckoInstruction inst)
{
	INSTRUCTION_START
	JITDISABLE(bJITPairedOff);
	FALLBACK_IF(inst.Rc);

	int d = inst.FD;
	int b = inst.FB;

	fpr.Lock(d, b);
	fpr.BindToRegister(d, d == b);

	switch (inst.SUBOP10)
	{
	case 40: //neg
		avx_op(&XEmitter::VPXOR, &XEmitter::PXOR, fpr.RX(d), fpr.R(b), M(psSignBits));
		break;
	case 136: //nabs
		avx_op(&XEmitter::VPOR, &XEmitter::POR, fpr.RX(d), fpr.R(b), M(psSignBits));
		break;
	case 264: //abs
		avx_op(&XEmitter::VPAND, &XEmitter::PAND, fpr.RX(d), fpr.R(b), M(psAbsMask));
		break;
	}

	fpr.UnlockAll();
}

void Jit64::ps_sum(UGeckoInstruction inst)
{
	INSTRUCTION_START
	JITDISABLE(bJITPairedOff);
	FALLBACK_IF(inst.Rc);

	int d = inst.FD;
	int a = inst.FA;
	int b = inst.FB;
	int c = inst.FC;
	fpr.Lock(a, b, c, d);
	OpArg op_a = fpr.R(a);
	fpr.BindToRegister(d, false);
	X64Reg tmp = d == b || d == c ? XMM0 : fpr.RX(d);
	MOVDDUP(tmp, op_a);   // {a.ps0, a.ps0}
	ADDPD(tmp, fpr.R(b)); // {a.ps0 + b.ps0, a.ps0 + b.ps1}
	switch (inst.SUBOP5)
	{
	case 10: // ps_sum0
		UNPCKHPD(tmp, fpr.R(c)); // {a.ps0 + b.ps1, c.ps1}
		break;
	case 11: // ps_sum1
		// {c.ps0, a.ps0 + b.ps1}
		if (fpr.R(c).IsSimpleReg())
		{
			if (cpu_info.bSSE4_1)
			{
				BLENDPD(tmp, fpr.R(c), 1);
			}
			else
			{
				MOVAPD(XMM1, fpr.R(c));
				SHUFPD(XMM1, R(tmp), 2);
				tmp = XMM1;
			}
		}
		else
		{
			MOVLPD(tmp, fpr.R(c));
		}
		break;
	default:
		PanicAlert("ps_sum WTF!!!");
	}
	ForceSinglePrecision(fpr.RX(d), R(tmp));
	SetFPRFIfNeeded(fpr.RX(d));
	fpr.UnlockAll();
}


void Jit64::ps_muls(UGeckoInstruction inst)
{
	INSTRUCTION_START
	JITDISABLE(bJITPairedOff);
	FALLBACK_IF(inst.Rc);

	int d = inst.FD;
	int a = inst.FA;
	int c = inst.FC;
	bool round_input = !jit->js.op->fprIsSingle[c];
	fpr.Lock(a, c, d);
	switch (inst.SUBOP5)
	{
	case 12: // ps_muls0
		MOVDDUP(XMM0, fpr.R(c));
		break;
	case 13: // ps_muls1
		avx_op(&XEmitter::VSHUFPD, &XEmitter::SHUFPD, XMM0, fpr.R(c), fpr.R(c), 3);
		break;
	default:
		PanicAlert("ps_muls WTF!!!");
	}
	if (round_input)
		Force25BitPrecision(XMM0, R(XMM0), XMM1);
	MULPD(XMM0, fpr.R(a));
	fpr.BindToRegister(d, false);
	ForceSinglePrecision(fpr.RX(d), R(XMM0));
	SetFPRFIfNeeded(fpr.RX(d));
	fpr.UnlockAll();
}


void Jit64::ps_mergeXX(UGeckoInstruction inst)
{
	INSTRUCTION_START
	JITDISABLE(bJITPairedOff);
	FALLBACK_IF(inst.Rc);

	int d = inst.FD;
	int a = inst.FA;
	int b = inst.FB;
	fpr.Lock(a, b, d);
	fpr.BindToRegister(d, d == a || d == b);

	switch (inst.SUBOP10)
	{
	case 528:
		avx_op(&XEmitter::VUNPCKLPD, &XEmitter::UNPCKLPD, fpr.RX(d), fpr.R(a), fpr.R(b));
		break; //00
	case 560:
		avx_op(&XEmitter::VSHUFPD, &XEmitter::SHUFPD, fpr.RX(d), fpr.R(a), fpr.R(b), 2);
		break; //01
	case 592:
		avx_op(&XEmitter::VSHUFPD, &XEmitter::SHUFPD, fpr.RX(d), fpr.R(a), fpr.R(b), 1);
		break; //10
	case 624:
		avx_op(&XEmitter::VUNPCKHPD, &XEmitter::UNPCKHPD, fpr.RX(d), fpr.R(a), fpr.R(b));
		break; //11
	default:
		_assert_msg_(DYNA_REC, 0, "ps_merge - invalid op");
	}
	fpr.UnlockAll();
}

void Jit64::ps_rsqrte(UGeckoInstruction inst)
{
	INSTRUCTION_START
	JITDISABLE(bJITFloatingPointOff);
	FALLBACK_IF(inst.Rc);
	int b = inst.FB;
	int d = inst.FD;

	gpr.FlushLockX(RSCRATCH_EXTRA);
	fpr.Lock(b, d);
	fpr.BindToRegister(b, true, false);
	fpr.BindToRegister(d, false);

	MOVSD(XMM0, fpr.R(b));
	CALL((void *)asm_routines.frsqrte);
	MOVSD(fpr.R(d), XMM0);

	MOVHLPS(XMM0, fpr.RX(b));
	CALL((void *)asm_routines.frsqrte);
	MOVLHPS(fpr.RX(d), XMM0);

	ForceSinglePrecision(fpr.RX(d), fpr.R(d));
	SetFPRFIfNeeded(fpr.RX(d));
	fpr.UnlockAll();
	gpr.UnlockAllX();
}

void Jit64::ps_res(UGeckoInstruction inst)
{
	INSTRUCTION_START
	JITDISABLE(bJITFloatingPointOff);
	FALLBACK_IF(inst.Rc);
	int b = inst.FB;
	int d = inst.FD;

	gpr.FlushLockX(RSCRATCH_EXTRA);
	fpr.Lock(b, d);
	fpr.BindToRegister(b, true, false);
	fpr.BindToRegister(d, false);

	MOVSD(XMM0, fpr.R(b));
	CALL((void *)asm_routines.fres);
	MOVSD(fpr.R(d), XMM0);

	MOVHLPS(XMM0, fpr.RX(b));
	CALL((void *)asm_routines.fres);
	MOVLHPS(fpr.RX(d), XMM0);

	ForceSinglePrecision(fpr.RX(d), fpr.R(d));
	SetFPRFIfNeeded(fpr.RX(d));
	fpr.UnlockAll();
	gpr.UnlockAllX();
}

//TODO: add optimized cases
void Jit64::ps_maddXX(UGeckoInstruction inst)
{
	INSTRUCTION_START
	JITDISABLE(bJITPairedOff);
	FALLBACK_IF(inst.Rc);

	int a = inst.FA;
	int b = inst.FB;
	int c = inst.FC;
	int d = inst.FD;
	bool fma = cpu_info.bFMA && !Core::g_want_determinism;
	bool round_input = !jit->js.op->fprIsSingle[c];
	fpr.Lock(a, b, c, d);

	if (fma)
		fpr.BindToRegister(b, true, false);

	if (inst.SUBOP5 == 14)
	{
		MOVDDUP(XMM0, fpr.R(c));
		if (round_input)
			Force25BitPrecision(XMM0, R(XMM0), XMM1);
	}
	else if (inst.SUBOP5 == 15)
	{
		avx_op(&XEmitter::VSHUFPD, &XEmitter::SHUFPD, XMM0, fpr.R(c), fpr.R(c), 3);
		if (round_input)
			Force25BitPrecision(XMM0, R(XMM0), XMM1);
	}
	else
	{
		if (round_input)
			Force25BitPrecision(XMM0, fpr.R(c), XMM1);
		else
			MOVAPD(XMM0, fpr.R(c));
	}

	if (fma)
	{
		switch (inst.SUBOP5)
		{
		case 14: //madds0
		case 15: //madds1
		case 29: //madd
			VFMADD132PD(XMM0, fpr.RX(b), fpr.R(a));
			break;
		case 28: //msub
			VFMSUB132PD(XMM0, fpr.RX(b), fpr.R(a));
			break;
		case 30: //nmsub
			VFNMADD132PD(XMM0, fpr.RX(b), fpr.R(a));
			break;
		case 31: //nmadd
			VFNMSUB132PD(XMM0, fpr.RX(b), fpr.R(a));
			break;
		}
	}
	else
	{
		switch (inst.SUBOP5)
		{
		case 14: //madds0
		case 15: //madds1
		case 29: //madd
			MULPD(XMM0, fpr.R(a));
			ADDPD(XMM0, fpr.R(b));
			break;
		case 28: //msub
			MULPD(XMM0, fpr.R(a));
			SUBPD(XMM0, fpr.R(b));
			break;
		case 30: //nmsub
			MULPD(XMM0, fpr.R(a));
			SUBPD(XMM0, fpr.R(b));
			PXOR(XMM0, M(psSignBits));
			break;
		case 31: //nmadd
			MULPD(XMM0, fpr.R(a));
			ADDPD(XMM0, fpr.R(b));
			PXOR(XMM0, M(psSignBits));
			break;
		default:
			_assert_msg_(DYNA_REC, 0, "ps_maddXX WTF!!!");
			return;
		}
	}

	fpr.BindToRegister(d, false);
	ForceSinglePrecision(fpr.RX(d), R(XMM0));
	SetFPRFIfNeeded(fpr.RX(d));
	fpr.UnlockAll();
}

void Jit64::ps_cmpXX(UGeckoInstruction inst)
{
	INSTRUCTION_START
	JITDISABLE(bJITFloatingPointOff);

	FloatCompare(inst, !!(inst.SUBOP10 & 64));
}
