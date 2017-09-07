/*
 * Copyright (C) 2015 Mihai Carabas <mihai.carabas@gmail.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/assym.h>
#include <vm/vm.h>
#include <vm/pmap.h>
#include <machine/vmm.h>

#include "arm64.h"

ASSYM(HYPCTX_REGS_X0, offsetof(struct hypctx, regs) + 0 * 8);
ASSYM(HYPCTX_REGS_X1, offsetof(struct hypctx, regs) + 1 * 8);
ASSYM(HYPCTX_REGS_X2, offsetof(struct hypctx, regs) + 2 * 8);
ASSYM(HYPCTX_REGS_X3, offsetof(struct hypctx, regs) + 3 * 8);
ASSYM(HYPCTX_REGS_X4, offsetof(struct hypctx, regs) + 4 * 8);
ASSYM(HYPCTX_REGS_X5, offsetof(struct hypctx, regs) + 5 * 8);
ASSYM(HYPCTX_REGS_X6, offsetof(struct hypctx, regs) + 6 * 8);
ASSYM(HYPCTX_REGS_X7, offsetof(struct hypctx, regs) + 7 * 8);
ASSYM(HYPCTX_REGS_X8, offsetof(struct hypctx, regs) + 8 * 8);
ASSYM(HYPCTX_REGS_X9, offsetof(struct hypctx, regs) + 9 * 8);
ASSYM(HYPCTX_REGS_X10, offsetof(struct hypctx, regs) + 10 * 8);
ASSYM(HYPCTX_REGS_X11, offsetof(struct hypctx, regs) + 11 * 8);
ASSYM(HYPCTX_REGS_X12, offsetof(struct hypctx, regs) + 12 * 8);
ASSYM(HYPCTX_REGS_X13, offsetof(struct hypctx, regs) + 13 * 8);
ASSYM(HYPCTX_REGS_X14, offsetof(struct hypctx, regs) + 14 * 8);
ASSYM(HYPCTX_REGS_X15, offsetof(struct hypctx, regs) + 15 * 8);
ASSYM(HYPCTX_REGS_X16, offsetof(struct hypctx, regs) + 16 * 8);
ASSYM(HYPCTX_REGS_X17, offsetof(struct hypctx, regs) + 17 * 8);
ASSYM(HYPCTX_REGS_X18, offsetof(struct hypctx, regs) + 18 * 8);
ASSYM(HYPCTX_REGS_X19, offsetof(struct hypctx, regs) + 19 * 8);
ASSYM(HYPCTX_REGS_X20, offsetof(struct hypctx, regs) + 20 * 8);
ASSYM(HYPCTX_REGS_X21, offsetof(struct hypctx, regs) + 21 * 8);
ASSYM(HYPCTX_REGS_X22, offsetof(struct hypctx, regs) + 22 * 8);
ASSYM(HYPCTX_REGS_X23, offsetof(struct hypctx, regs) + 23 * 8);
ASSYM(HYPCTX_REGS_X24, offsetof(struct hypctx, regs) + 24 * 8);
ASSYM(HYPCTX_REGS_X25, offsetof(struct hypctx, regs) + 25 * 8);
ASSYM(HYPCTX_REGS_X26, offsetof(struct hypctx, regs) + 26 * 8);
ASSYM(HYPCTX_REGS_X27, offsetof(struct hypctx, regs) + 27 * 8);
ASSYM(HYPCTX_REGS_X28, offsetof(struct hypctx, regs) + 28 * 8);
ASSYM(HYPCTX_REGS_X29, offsetof(struct hypctx, regs) + 29 * 8);
ASSYM(HYPCTX_REGS_LR, offsetof(struct hypctx, regs.lr));
ASSYM(HYPCTX_REGS_SP, offsetof(struct hypctx, regs.sp));
ASSYM(HYPCTX_REGS_ELR, offsetof(struct hypctx, regs.elr));
ASSYM(HYPCTX_REGS_SPSR, offsetof(struct hypctx, regs.spsr));

ASSYM(HYPCTX_ACTLR_EL1, offsetof(struct hypctx, actlr_el1));
ASSYM(HYPCTX_AMAIR_EL1, offsetof(struct hypctx, amair_el1));
ASSYM(HYPCTX_ELR_EL1, offsetof(struct hypctx, elr_el1));
ASSYM(HYPCTX_PAR_EL1, offsetof(struct hypctx, par_el1));
ASSYM(HYPCTX_MAIR_EL1, offsetof(struct hypctx, mair_el1));
ASSYM(HYPCTX_TCR_EL1, offsetof(struct hypctx, tcr_el1));
ASSYM(HYPCTX_TPIDR_EL0, offsetof(struct hypctx, tpidr_el0));
ASSYM(HYPCTX_TPIDR_EL1, offsetof(struct hypctx, tpidr_el1));
ASSYM(HYPCTX_TPIDRRO_EL0, offsetof(struct hypctx, tpidrro_el0));
ASSYM(HYPCTX_TTBR0_EL1, offsetof(struct hypctx, ttbr0_el1));
ASSYM(HYPCTX_TTBR1_EL1, offsetof(struct hypctx, ttbr1_el1));
ASSYM(HYPCTX_VBAR_EL1, offsetof(struct hypctx, vbar_el1));
ASSYM(HYPCTX_AFSR0_EL1, offsetof(struct hypctx, afsr0_el1));
ASSYM(HYPCTX_AFSR1_EL1, offsetof(struct hypctx, afsr1_el1));
ASSYM(HYPCTX_CONTEXTIDR_EL1, offsetof(struct hypctx, contextidr_el1));
ASSYM(HYPCTX_CPACR_EL1, offsetof(struct hypctx, cpacr_el1));
ASSYM(HYPCTX_ESR_EL1, offsetof(struct hypctx, esr_el1));
ASSYM(HYPCTX_FAR_EL1, offsetof(struct hypctx, far_el1));
ASSYM(HYPCTX_SCTLR_EL1, offsetof(struct hypctx, sctlr_el1));
ASSYM(HYPCTX_SPSR_EL1, offsetof(struct hypctx, spsr_el1));

ASSYM(HYPCTX_ELR_EL2, offsetof(struct hypctx, elr_el2));
ASSYM(HYPCTX_HCR_EL2, offsetof(struct hypctx, hcr_el2));
ASSYM(HYPCTX_VPIDR_EL2, offsetof(struct hypctx, vpidr_el2));
ASSYM(HYPCTX_VMPIDR_EL2, offsetof(struct hypctx, vmpidr_el2));
ASSYM(HYPCTX_CPTR_EL2, offsetof(struct hypctx, cptr_el2));
ASSYM(HYPCTX_SPSR_EL2, offsetof(struct hypctx, spsr_el2));

ASSYM(HYPCTX_HYP, offsetof(struct hypctx, hyp));

ASSYM(HYP_VTTBR, offsetof(struct hyp, vttbr));
ASSYM(HYP_VTIMER_ENABLED, offsetof(struct hyp, vtimer.enabled));
ASSYM(HYP_VTIMER_CNTVOFF, offsetof(struct hyp, vtimer.cntvoff));

ASSYM(HYPCTX_EXIT_INFO_ESR_EL2, offsetof(struct hypctx, exit_info.esr_el2));
ASSYM(HYPCTX_EXIT_INFO_FAR_EL2, offsetof(struct hypctx, exit_info.far_el2));
ASSYM(HYPCTX_EXIT_INFO_HPFAR_EL2, offsetof(struct hypctx, exit_info.hpfar_el2));

ASSYM(HYPCTX_VGIC_INT_CTRL, offsetof(struct hypctx, vgic_cpu_int.virtual_int_ctrl));
ASSYM(HYPCTX_VGIC_LR_NUM, offsetof(struct hypctx, vgic_cpu_int.lr_num));
ASSYM(HYPCTX_VGIC_HCR, offsetof(struct hypctx, vgic_cpu_int.hcr));
ASSYM(HYPCTX_VGIC_VMCR, offsetof(struct hypctx, vgic_cpu_int.vmcr));
ASSYM(HYPCTX_VGIC_MISR, offsetof(struct hypctx, vgic_cpu_int.misr));
ASSYM(HYPCTX_VGIC_EISR, offsetof(struct hypctx, vgic_cpu_int.eisr));
ASSYM(HYPCTX_VGIC_ELSR, offsetof(struct hypctx, vgic_cpu_int.elsr));
ASSYM(HYPCTX_VGIC_APR, offsetof(struct hypctx, vgic_cpu_int.apr));
ASSYM(HYPCTX_VGIC_LR, offsetof(struct hypctx, vgic_cpu_int.lr));

ASSYM(HYPCTX_VTIMER_CPU_CNTV_CTL, offsetof(struct hypctx, vtimer_cpu.cntv_ctl));
ASSYM(HYPCTX_VTIMER_CPU_CNTV_CVAL, offsetof(struct hypctx, vtimer_cpu.cntv_cval));

#ifdef VFP
ASSYM(HYPCTX_HOST_VFP_STATE, offsetof(struct hypctx, host_vfp_state));
ASSYM(HYPCTX_GUEST_VFP_STATE, offsetof(struct hypctx, guest_vfp_state));
#endif
