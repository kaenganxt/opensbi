/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2019 Western Digital Corporation or its affiliates.
 *
 * Authors:
 *   Anup Patel <anup.patel@wdc.com>
 */

#include <sbi/riscv_asm.h>
#include <sbi/riscv_encoding.h>
#include <sbi/sbi_bitops.h>
#include <sbi/sbi_emulate_csr.h>
#include <sbi/sbi_error.h>
#include <sbi/sbi_illegal_insn.h>
#include <sbi/sbi_pmu.h>
#include <sbi/sbi_trap.h>
#include <sbi/sbi_unpriv.h>

#define ENABLE_MPRV(mstatus) asm volatile( \
		"csrrs %[mstatus], " STR(CSR_MSTATUS) ", %[mprv]\n" \
		: [mstatus] "+&r"(mstatus) \
		: [mprv] "r"(MSTATUS_MPRV) \
		: \
	);
#define DISABLE_MPRV(mstatus) asm volatile( \
		"csrw " STR(CSR_MSTATUS) ", %[mstatus]\n" \
		: [mstatus] "+&r"(mstatus) \
	);

typedef int (*illegal_insn_func)(ulong insn, struct sbi_trap_regs *regs);

static int truly_illegal_insn(ulong insn, struct sbi_trap_regs *regs)
{
	struct sbi_trap_info trap;

	trap.epc = regs->mepc;
	trap.cause = CAUSE_ILLEGAL_INSTRUCTION;
	trap.tval = insn;
	trap.tval2 = 0;
	trap.tinst = 0;

	return sbi_trap_redirect(regs, &trap);
}

static int amo_insn(ulong insn, struct sbi_trap_regs *regs) {
	// TODO: Only works for rv32 right now
	int action = (insn >> 27);
	if ((action & 0x2) == 2) { // Is lr/sc
		return truly_illegal_insn(insn, regs);
	}
	register ulong rs1_val = GET_RS1(insn, regs);
	ulong rs2_val = GET_RS2(insn, regs);
	register int aq = (insn >> 26) & 0x1;
	register int rl = (insn >> 25) & 0x1;

	register int return_val;
	register unsigned int old_value;
	register unsigned int new_value;
	register ulong mstatus = 0;

	do {
		ENABLE_MPRV(mstatus);
		if (aq) {
			asm volatile("lr.w.aq %0, (%1)" : "=r"(old_value) : "r"(rs1_val));
		} else {
			asm volatile("lr.w %0, (%1)" : "=r"(old_value) : "r"(rs1_val));
		}
		DISABLE_MPRV(mstatus);

		switch (action) {
			case 0b00000: // amoadd
				new_value = old_value + rs2_val;
				break;
			case 0b00001: // amoswap
				new_value = rs2_val;
				break;
			case 0b00100: // amoxor
				new_value = old_value ^ rs2_val;
				break;
			case 0b01000: // amoor
				new_value = old_value | rs2_val;
				break;
			case 0b01100: // amoand
				new_value = old_value & rs2_val;
				break;
			case 0b10000: // amomin
				if (((int) old_value) > ((int) rs2_val)) {
					new_value = rs2_val;
				} else {
					new_value = old_value;
				}
				break;
			case 0b10100: // amomax
				if (((int) old_value) < ((int) rs2_val)) {
					new_value = rs2_val;
				} else {
					new_value = old_value;
				}
				break;
			case 0b11000: // amominu
				if (old_value > rs2_val) {
					new_value = rs2_val;
				} else {
					new_value = old_value;
				}
				break;
			case 0b11100: // amomaxu
				if (old_value < rs2_val) {
					new_value = rs2_val;
				} else {
					new_value = old_value;
				}
				break;
			default:
				return truly_illegal_insn(insn, regs);
		}

		ENABLE_MPRV(mstatus);
		if (rl) {
			asm volatile("sc.w.rl %0, %1, (%2)" : "=r"(return_val) : "r"(new_value), "r"(rs1_val));
		} else {
			asm volatile("sc.w %0, %1, (%2)" : "=r"(return_val) : "r"(new_value), "r"(rs1_val));
		}
		DISABLE_MPRV(mstatus);
	} while(return_val);

	SET_RD(insn, regs, old_value);

	regs->mepc += 4;
	return 0;
}

static int system_opcode_insn(ulong insn, struct sbi_trap_regs *regs)
{
	int do_write, rs1_num = (insn >> 15) & 0x1f;
	ulong rs1_val = GET_RS1(insn, regs);
	int csr_num   = (u32)insn >> 20;
	ulong csr_val, new_csr_val;

	/* TODO: Ensure that we got CSR read/write instruction */

	if (sbi_emulate_csr_read(csr_num, regs, &csr_val))
		return truly_illegal_insn(insn, regs);

	do_write = rs1_num;
	switch (GET_RM(insn)) {
	case 1:
		new_csr_val = rs1_val;
		do_write    = 1;
		break;
	case 2:
		new_csr_val = csr_val | rs1_val;
		break;
	case 3:
		new_csr_val = csr_val & ~rs1_val;
		break;
	case 5:
		new_csr_val = rs1_num;
		do_write    = 1;
		break;
	case 6:
		new_csr_val = csr_val | rs1_num;
		break;
	case 7:
		new_csr_val = csr_val & ~rs1_num;
		break;
	default:
		return truly_illegal_insn(insn, regs);
	};

	if (do_write && sbi_emulate_csr_write(csr_num, regs, new_csr_val))
		return truly_illegal_insn(insn, regs);

	SET_RD(insn, regs, csr_val);

	regs->mepc += 4;

	return 0;
}

static illegal_insn_func illegal_insn_table[32] = {
	truly_illegal_insn, /* 0 */
	truly_illegal_insn, /* 1 */
	truly_illegal_insn, /* 2 */
	truly_illegal_insn, /* 3 */
	truly_illegal_insn, /* 4 */
	truly_illegal_insn, /* 5 */
	truly_illegal_insn, /* 6 */
	truly_illegal_insn, /* 7 */
	truly_illegal_insn, /* 8 */
	truly_illegal_insn, /* 9 */
	truly_illegal_insn, /* 10 */
	amo_insn,           /* 11 */
	truly_illegal_insn, /* 12 */
	truly_illegal_insn, /* 13 */
	truly_illegal_insn, /* 14 */
	truly_illegal_insn, /* 15 */
	truly_illegal_insn, /* 16 */
	truly_illegal_insn, /* 17 */
	truly_illegal_insn, /* 18 */
	truly_illegal_insn, /* 19 */
	truly_illegal_insn, /* 20 */
	truly_illegal_insn, /* 21 */
	truly_illegal_insn, /* 22 */
	truly_illegal_insn, /* 23 */
	truly_illegal_insn, /* 24 */
	truly_illegal_insn, /* 25 */
	truly_illegal_insn, /* 26 */
	truly_illegal_insn, /* 27 */
	system_opcode_insn, /* 28 */
	truly_illegal_insn, /* 29 */
	truly_illegal_insn, /* 30 */
	truly_illegal_insn  /* 31 */
};

int sbi_illegal_insn_handler(ulong insn, struct sbi_trap_regs *regs)
{
	struct sbi_trap_info uptrap;

	/*
	 * We only deal with 32-bit (or longer) illegal instructions. If we
	 * see instruction is zero OR instruction is 16-bit then we fetch and
	 * check the instruction encoding using unprivilege access.
	 *
	 * The program counter (PC) in RISC-V world is always 2-byte aligned
	 * so handling only 32-bit (or longer) illegal instructions also help
	 * the case where MTVAL CSR contains instruction address for illegal
	 * instruction trap.
	 */

	sbi_pmu_ctr_incr_fw(SBI_PMU_FW_ILLEGAL_INSN);
	if (unlikely((insn & 3) != 3)) {
		insn = sbi_get_insn(regs->mepc, &uptrap);
		if (uptrap.cause) {
			uptrap.epc = regs->mepc;
			return sbi_trap_redirect(regs, &uptrap);
		}
		if ((insn & 3) != 3)
			return truly_illegal_insn(insn, regs);
	}

	return illegal_insn_table[(insn & 0x7c) >> 2](insn, regs);
}
