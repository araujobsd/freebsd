/*
 * Copyright (C) 2018 Alexandru Elisei <alexandru.elisei@gmail.com>
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

#include <sys/types.h>
#include <sys/errno.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/rman.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/smp.h>
#include <sys/bitstring.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <dev/ofw/openfirm.h>

#include <machine/bus.h>
#include <machine/bitops.h>
#include <machine/cpufunc.h>
#include <machine/cpu.h>
#include <machine/param.h>
#include <machine/pmap.h>
#include <machine/vmparam.h>
#include <machine/intr.h>
#include <machine/vmm.h>
#include <machine/vmm_instruction_emul.h>

#include <arm/arm/gic_common.h>
#include <arm64/arm64/gic_v3_reg.h>
#include <arm64/arm64/gic_v3_var.h>

#include <arm64/vmm/hyp.h>
#include <arm64/vmm/mmu.h>
#include <arm64/vmm/arm64.h>

#include "vgic_v3.h"
#include "vgic_v3_reg.h"

#define VGIC_V3_DEVNAME		"vgic"
#define VGIC_V3_DEVSTR		"ARM Virtual Generic Interrupt Controller v3"

#define	RES0			0UL

#define	PENDING_SIZE_MIN	32
#define	PENDING_SIZE_MAX	(1 << 10)
#define	PENDING_INVALID		(GIC_LAST_SPI + 1)

#define	lr_pending(lr)		\
    (ICH_LR_EL2_STATE(lr) == ICH_LR_EL2_STATE_PENDING)
#define	lr_inactive(lr)	\
    (ICH_LR_EL2_STATE(lr) == ICH_LR_EL2_STATE_INACTIVE)
#define	lr_not_active(lr) (lr_pending(lr) || lr_inactive(lr))

MALLOC_DEFINE(M_VGIC_V3, "ARM VMM VGIC V3", "ARM VMM VGIC V3");

extern uint64_t hypmode_enabled;

struct vgic_v3_virt_features {
	uint8_t min_prio;
	size_t ich_lr_num;
	size_t ich_ap0r_num;
	size_t ich_ap1r_num;
};
static struct vgic_v3_virt_features virt_features;

struct vgic_v3_ro_regs {
	uint32_t gicd_icfgr0;
	uint32_t gicd_pidr2;
	uint32_t gicd_typer;
};
static struct vgic_v3_ro_regs ro_regs;

void
vgic_v3_cpuinit(void *arg, bool last_vcpu)
{
	struct hypctx *hypctx = arg;
	struct vgic_v3_cpu_if *cpu_if = &hypctx->vgic_cpu_if;
	struct vgic_v3_redist *redist = &hypctx->vgic_redist;
	uint64_t aff, vmpidr_el2;

	vmpidr_el2 = hypctx->vmpidr_el2;
	/*
	 * Get affinity for the current CPU. The guest CPU affinity is taken
	 * from VMPIDR_EL2. The Redistributor corresponding to this CPU is
	 * the Redistributor with the same affinity from GICR_TYPER.
	 */
	aff = (CPU_AFF3(vmpidr_el2) << 24) | (CPU_AFF2(vmpidr_el2) << 16) |
	    (CPU_AFF1(vmpidr_el2) << 8) | CPU_AFF0(vmpidr_el2);

	/* Set up GICR_TYPER. */
	redist->gicr_typer = aff << GICR_TYPER_AFF_SHIFT;
	/* Redistributor doesn't support virtual or physical LPIS. */
	redist->gicr_typer &= ~GICR_TYPER_VLPIS;
	redist->gicr_typer &= ~GICR_TYPER_PLPIS;

	if (last_vcpu)
		/* Mark the last Redistributor */
		redist->gicr_typer |= GICR_TYPER_LAST;

	/*
	 * Configure the Redistributor Control Register.
	 *
	 * ~GICR_CTLR_LPI_ENABLE: LPIs are disabled
	 */
	redist->gicr_ctlr = 0 & ~GICR_CTLR_LPI_ENABLE;

	mtx_init(&cpu_if->lr_mtx, "VGICv3 ICH_LR_EL2 lock", NULL, MTX_SPIN);

	/*
	 * Configure the Interrupt Controller Hyp Control Register.
	 *
	 * ICH_HCR_EL2_En: enable virtual CPU interface.
	 *
	 * Maintenance interrupts are disabled.
	 */
	cpu_if->ich_hcr_el2 = ICH_HCR_EL2_En;

	/*
	 * Configure the Interrupt Controller Virtual Machine Control Register.
	 *
	 * ICH_VMCR_EL2_VPMR: lowest priority mask for the VCPU interface
	 * ICH_VMCR_EL2_VBPR1_NO_PREEMPTION: disable interrupt preemption for
	 * Group 1 interrupts
	 * ICH_VMCR_EL2_VBPR0_NO_PREEMPTION: disable interrupt preemption for
	 * Group 0 interrupts
	 * ~ICH_VMCR_EL2_VEOIM: writes to EOI registers perform priority drop
	 * and interrupt deactivation.
	 * ICH_VMCR_EL2_VENG0: virtual Group 0 interrupts enabled.
	 * ICH_VMCR_EL2_VENG1: virtual Group 1 interrupts enabled.
	 */
	cpu_if->ich_vmcr_el2 = \
	    (virt_features.min_prio << ICH_VMCR_EL2_VPMR_SHIFT) | \
	    ICH_VMCR_EL2_VBPR1_NO_PREEMPTION | ICH_VMCR_EL2_VBPR0_NO_PREEMPTION;
	cpu_if->ich_vmcr_el2 &= ~ICH_VMCR_EL2_VEOIM;
	cpu_if->ich_vmcr_el2 |= ICH_VMCR_EL2_VENG0 | ICH_VMCR_EL2_VENG1;

	cpu_if->ich_lr_num = virt_features.ich_lr_num;
	cpu_if->ich_ap0r_num = virt_features.ich_ap0r_num;
	cpu_if->ich_ap1r_num = virt_features.ich_ap1r_num;

	cpu_if->pending = malloc(PENDING_SIZE_MIN * sizeof(*cpu_if->pending),
	    M_VGIC_V3, M_WAITOK | M_ZERO);
	cpu_if->pending_size = PENDING_SIZE_MIN;
	cpu_if->pending_num = 0;
}

void
vgic_v3_vminit(void *arg)
{
	struct hyp *hyp = arg;
	struct vgic_v3_dist *dist = &hyp->vgic_dist;

	/*
	 * Configure the Distributor control register.
	 *
	 * GICD_CTLR_G1: enable Group 0 interrupts
	 * GICD_CTLR_G1A: enable Group 1 interrupts
	 * GICD_CTLR_ARE_NS: enable affinity routing
	 * GICD_CTLR_DS: ARM GIC Architecture Specification for GICv3 and
	 * GICv4, p. 4-464: when the distributor supports a single security
	 * state, this bit is RAO/WI
	 */
	dist->gicd_ctlr = GICD_CTLR_G1 | GICD_CTLR_G1A | GICD_CTLR_ARE_NS | \
	    GICD_CTLR_DS;

	dist->gicd_typer = ro_regs.gicd_typer;
	dist->nirqs = GICD_TYPER_I_NUM(dist->gicd_typer);
	dist->gicd_pidr2 = ro_regs.gicd_pidr2;
}

int
vgic_v3_attach_to_vm(void *arg, uint64_t dist_start, size_t dist_size,
    uint64_t redist_start, size_t redist_size)
{
	struct hyp *hyp = arg;
	struct hypctx *hypctx;
	struct vgic_v3_dist *dist = &hyp->vgic_dist;
	struct vgic_v3_redist *redist;
	int i;

	/* Set the distributor address and size for trapping guest access. */
	dist->start = dist_start;
	dist->end = dist_start + dist_size;

	hyp->vgic_mmio_regions = \
	    malloc(VGIC_MEM_REGION_LAST * sizeof(*hyp->vgic_mmio_regions),
	    M_VGIC_V3, M_WAITOK | M_ZERO);
	dist_mmio_init(hyp);

	for (i = 0; i < VM_MAXCPU; i++) {
		hypctx = &hyp->ctx[i];
		redist = &hypctx->vgic_redist;

		/* Set the redistributor address and size. */
		redist->start = redist_start;
		redist->end = redist_start + redist_size;
		redist_mmio_init(hypctx);
	}

	hyp->vgic_attached = true;

	return (0);
}

/* TODO: call this on VM destroy. */
static void vgic_v3_detach_from_vm(void *arg)
{
	struct hyp *hyp = arg;
	struct hypctx *hypctx;
	int i;

	for (i = 0; i < VM_MAXCPU; i++) {
		hypctx = &hyp->ctx[i];
		redist_mmio_destroy(hypctx);
	}

	dist_mmio_destroy(hyp);
	free(hyp->vgic_mmio_regions, M_VGIC_V3);
}

int
vgic_v3_vcpu_pending_irq(void *arg)
{
	struct hypctx *hypctx = arg;
	struct vgic_v3_cpu_if *cpu_if = &hypctx->vgic_cpu_if;

	return (cpu_if->pending_num);
}

static int
vgic_v3_remove_pending_unsafe(struct virq *virq, struct vgic_v3_cpu_if *cpu_if)
{
	size_t dest = 0;
	size_t from = cpu_if->pending_num;

	while (dest < cpu_if->pending_num) {
		if (cpu_if->pending[dest].irq == virq->irq) {
			for (from = dest + 1; from < cpu_if->pending_num; from++) {
				if (cpu_if->pending[from].irq == virq->irq)
					continue;
				cpu_if->pending[dest++] = cpu_if->pending[from];
			}
			cpu_if->pending_num = dest;
		} else {
			dest++;
		}
	}

	return (from - dest);
}

int
vgic_v3_remove_irq(void *arg, struct virq *virq)
{
        struct hypctx *hypctx = arg;
	struct vgic_v3_cpu_if *cpu_if = &hypctx->vgic_cpu_if;
	struct vgic_v3_dist *dist = &hypctx->hyp->vgic_dist;
	size_t i;

	if (virq->irq >= dist->nirqs ||
	    virq->type >= VIRQ_TYPE_INVALID) {
		eprintf("Malformed virq\n");
		return (1);
	}

	mtx_lock_spin(&cpu_if->lr_mtx);

	for (i = 0; i < cpu_if->ich_lr_num; i++)
		if (ICH_LR_EL2_VINTID(cpu_if->ich_lr_el2[i]) == virq->irq &&
		    lr_not_active(cpu_if->ich_lr_el2[i]))
			cpu_if->ich_lr_el2[i] &= ~ICH_LR_EL2_STATE_MASK;

	vgic_v3_remove_pending_unsafe(virq, cpu_if);

	mtx_unlock_spin(&cpu_if->lr_mtx);

	return (0);
}

static int
vgic_v3_add_pending_unsafe(struct virq *virq, struct vgic_v3_cpu_if *cpu_if)
{
	struct virq *new_pending, *old_pending;
	size_t new_size;

	if (cpu_if->pending_num == cpu_if->pending_size) {
		/* Double the size of the pending list */
		new_size = cpu_if->pending_size << 1;
		if (new_size > PENDING_SIZE_MAX)
			return (1);

		new_pending = malloc(new_size * sizeof(*cpu_if->pending),
		    M_VGIC_V3, M_WAITOK | M_ZERO);
		memcpy(new_pending, cpu_if->pending,
		    cpu_if->pending_size * sizeof(*virq));

		old_pending = cpu_if->pending;
		cpu_if->pending = new_pending;
		cpu_if->pending_size = new_size;
		free(old_pending, M_VGIC_V3);
	}

	memcpy(&cpu_if->pending[cpu_if->pending_num], virq, sizeof(*virq));
	cpu_if->pending_num++;

	return (0);
}

int
vgic_v3_inject_irq(void *arg, struct virq *virq)
{
        struct hypctx *hypctx = arg;
	struct vgic_v3_cpu_if *cpu_if = &hypctx->vgic_cpu_if;
	struct vgic_v3_dist *dist = &hypctx->hyp->vgic_dist;
	int error;

	KASSERT(virq->irq > GIC_LAST_SGI, ("SGI interrupts not implemented"));

	if (virq->irq >= dist->nirqs ||
	    virq->type >= VIRQ_TYPE_INVALID) {
		eprintf("Malformed IRQ %u.\n", virq->irq);
		return (1);
	}

	mtx_lock_spin(&cpu_if->lr_mtx);
	error = vgic_v3_add_pending_unsafe(virq, cpu_if);
	if (error)
		eprintf("Unable to mark IRQ %u as pending.\n", virq->irq);
	mtx_unlock_spin(&cpu_if->lr_mtx);

	return (error);
}

static uint8_t
vgic_v3_get_priority(struct virq *virq, struct hypctx *hypctx)
{
	struct vgic_v3_dist *dist = &hypctx->hyp->vgic_dist;
	struct vgic_v3_redist *redist = &hypctx->vgic_redist;
	size_t n;
	uint32_t off, mask;
	uint8_t priority;

	n = virq->irq / 4;
	off = n % 4;
	mask = 0xff << off;
	/*
	 * When affinity routing is enabled, the Redistributor is used for
	 * SGIs and PPIs and the Distributor for SPIs. When affinity routing
	 * is not enabled, the Distributor registers are used for all
	 * interrupts.
	 */
	if ((dist->gicd_ctlr & GICD_CTLR_ARE_NS) && (n <= 7))
		priority = (redist->gicr_ipriorityr[n] & mask) >> off;
	else
		priority = (dist->gicd_ipriorityr[n] & mask) >> off;

	return (priority);
}

static bool
vgic_v3_int_target(struct virq *virq, struct hypctx *hypctx)
{
	struct vgic_v3_dist *dist = &hypctx->hyp->vgic_dist;
	struct vgic_v3_redist *redist = &hypctx->vgic_redist;
	uint64_t irouter;
	uint64_t aff;

	if (virq->irq <= GIC_LAST_PPI)
		return (true);

	/* XXX Affinity routing disabled not implemented */
	if (!(dist->gicd_ctlr & GICD_CTLR_ARE_NS))
		return (true);

	irouter = dist->gicd_irouter[virq->irq];
	/* Check if 1-of-N routing is active */
	if (irouter & GICD_IROUTER_IRM) {
		/* Check if the VCPU is participating */
		if (redist->gicr_ctlr & GICR_CTLR_DPG1NS)
			return (false);
		else
			return (true);
	}

	aff = redist->gicr_typer >> GICR_TYPER_AFF_SHIFT;
	/* Affinity in format for comparison with irouter */
	aff = GICR_TYPER_AFF0(redist->gicr_typer) | \
	    (GICR_TYPER_AFF1(redist->gicr_typer) << 8) | \
	    (GICR_TYPER_AFF2(redist->gicr_typer) << 16) | \
	    (GICR_TYPER_AFF3(redist->gicr_typer) << 32);
	if ((irouter & aff) == aff)
		return (true);
	else
		return (false);
}

static bool
vgic_v3_int_enabled(struct virq *virq, struct hypctx *hypctx, int *group)
{
	struct vgic_v3_dist *dist = &hypctx->hyp->vgic_dist;
	struct vgic_v3_redist *redist = &hypctx->vgic_redist;
	struct vgic_v3_cpu_if *cpu_if = &hypctx->vgic_cpu_if;
	uint32_t irq_off, irq_mask;
	int n;

	irq_off = virq->irq % 32;
	irq_mask = 1 << irq_off;
	n = virq->irq / 32;

	/* XXX GIC{R, D}_IGROUPMODR set the secure/non-secure bit */
	if (n == 0)
		*group = (redist->gicr_igroupr0 & irq_mask) ? 1 : 0;
	else
		*group = (dist->gicd_igroupr[n] & irq_mask) ? 1 : 0;

	/*
	 * Check that the interrupt group hasn't been disabled:
	 * - in the Distributor
	 * - in the CPU interface
	 */
	if (*group == 1) {
		if (!(dist->gicd_ctlr & GICD_CTLR_G1A))
			return (false);
		if (!(cpu_if->ich_vmcr_el2 & ICH_VMCR_EL2_VENG1))
			return (false);
	} else {
		if (!(dist->gicd_ctlr & GICD_CTLR_G1))
			return (false);
		if (!(cpu_if->ich_vmcr_el2 & ICH_VMCR_EL2_VENG0))
			return (false);
	}

	/* Check that the interrupt ID hasn't been disabled */
	if (virq->irq <= GIC_LAST_PPI) {
		if (!(redist->gicr_ixenabler0 & irq_mask))
			return (false);
	} else {
		if (!(dist->gicd_ixenabler[n] & irq_mask))
			return (false);
	}

	return (true);
}

static struct virq *
vgic_v3_highest_priority_pending(struct vgic_v3_cpu_if *cpu_if,
    struct hypctx *hypctx, int *group)
{
	int i, max_idx;
	uint8_t priority, max_priority;
	uint8_t vpmr;

	vpmr = (cpu_if->ich_vmcr_el2 & ICH_VMCR_EL2_VPMR_MASK) >> \
	    ICH_VMCR_EL2_VPMR_SHIFT;

	max_idx = -1;
	max_priority = 0xff;
	for (i = 0; i < cpu_if->pending_num; i++) {
		/* Check that the interrupt hasn't been already scheduled */
		if (cpu_if->pending[i].irq == PENDING_INVALID)
			continue;

		if (!vgic_v3_int_enabled(&cpu_if->pending[i], hypctx, group))
			continue;

		if (!vgic_v3_int_target(&cpu_if->pending[i], hypctx))
			continue;

		priority = vgic_v3_get_priority(&cpu_if->pending[i], hypctx);
		if (priority >= vpmr)
			continue;

		if (max_idx == -1) {
			max_idx = i;
			max_priority = priority;
		} else if (priority > max_priority) {
			max_idx = i;
			max_priority = priority;
		} else if (priority == max_priority &&
		    cpu_if->pending[i].type < cpu_if->pending[max_idx].type) {
			max_idx = i;
			max_priority = priority;
		}
	}

	if (max_idx == -1)
		return (NULL);
	return (&cpu_if->pending[max_idx]);
}

void
vgic_v3_sync_hwstate(void *arg)
{
	struct hypctx *hypctx = arg;
	struct vgic_v3_cpu_if *cpu_if = &hypctx->vgic_cpu_if;
	struct virq *virq;
	struct virq invalid_virq, tmp;
	uint8_t priority;
	int group;
	int i;
	int error;

	mtx_lock_spin(&cpu_if->lr_mtx);

	if (cpu_if->pending_num == 0)
		goto out;

	invalid_virq.irq = PENDING_INVALID;
	invalid_virq.type = VIRQ_TYPE_INVALID;

	/*
	 * Add all interrupts from the list registers that are not active to
	 * pending buffer to be rescheduled in the next step.
	 */
	for (i = 0; i < cpu_if->ich_lr_num; i++)
		if (lr_pending(cpu_if->ich_lr_el2[i])) {
			tmp.irq = cpu_if->ich_lr_el2[i] & ICH_LR_EL2_VINTID_MASK;
			tmp.type = VIRQ_TYPE_MAXPRIO;
			error = vgic_v3_add_pending_unsafe(&tmp, cpu_if);
			if (error)
				/* Pending list full, stop it */
				break;
			cpu_if->ich_lr_el2[i] &= ~ICH_LR_EL2_STATE_MASK;
		}

	for (i = 0; i < cpu_if->ich_lr_num; i++) {
		if (!lr_inactive(cpu_if->ich_lr_el2[i]))
			continue;

		virq = vgic_v3_highest_priority_pending(cpu_if, hypctx, &group);
		if (virq == NULL)
			/* No more pending interrupts */
			break;

		priority = vgic_v3_get_priority(virq, hypctx);

		cpu_if->ich_lr_el2[i] = ICH_LR_EL2_STATE_PENDING;
		cpu_if->ich_lr_el2[i] |= (uint64_t)group << ICH_LR_EL2_GROUP_SHIFT;
		cpu_if->ich_lr_el2[i] |= (uint64_t)priority << ICH_LR_EL2_PRIO_SHIFT;
		cpu_if->ich_lr_el2[i] |= virq->irq;

		/* Mark the scheduled pending interrupt as invalid */
		*virq = invalid_virq;
	}
	/* Remove all scheduled interrupts */
	vgic_v3_remove_pending_unsafe(&invalid_virq, cpu_if);

	/* TODO Enable maintenance interrupts if interrupts are still pending */

out:
	mtx_unlock_spin(&cpu_if->lr_mtx);
}

void
vgic_v3_init(uint64_t ich_vtr_el2) {
	uint32_t pribits, prebits;

	pribits = ICH_VTR_EL2_PRIBITS(ich_vtr_el2);
	switch (pribits) {
	case 5:
		virt_features.min_prio = 0xf8;
	case 6:
		virt_features.min_prio = 0xfc;
	case 7:
		virt_features.min_prio = 0xfe;
	case 8:
		virt_features.min_prio = 0xff;
	}

	prebits = ICH_VTR_EL2_PREBITS(ich_vtr_el2);
	switch (prebits) {
	case 5:
		virt_features.ich_ap0r_num = 1;
		virt_features.ich_ap1r_num = 1;
	case 6:
		virt_features.ich_ap0r_num = 2;
		virt_features.ich_ap1r_num = 2;
	case 7:
		virt_features.ich_ap0r_num = 4;
		virt_features.ich_ap1r_num = 4;
	}

	virt_features.ich_lr_num = ICH_VTR_EL2_LISTREGS(ich_vtr_el2);
}

static int
arm_vgic_detach(device_t dev)
{
	return (0);
}

static void vgic_v3_set_ro_regs(device_t dev)
{
	device_t gic;
	struct gic_v3_softc *gic_sc;

	gic = device_get_parent(dev);
	gic_sc = device_get_softc(gic);

	/* GICD_ICFGR0 configures SGIs and it is read-only. */
	ro_regs.gicd_icfgr0 = gic_d_read(gic_sc, 4, GICD_ICFGR(0));

	/*
	 * Configure the GIC type register for the guest.
	 *
	 * ~GICD_TYPER_SECURITYEXTN: disable security extensions.
	 * ~GICD_TYPER_DVIS: direct injection for virtual LPIs not supported.
	 * ~GICD_TYPER_LPIS: LPIs not supported.
	 */
	ro_regs.gicd_typer = gic_d_read(gic_sc, 4, GICD_TYPER);
	ro_regs.gicd_typer &= ~GICD_TYPER_SECURITYEXTN;
	ro_regs.gicd_typer &= ~GICD_TYPER_DVIS;
	ro_regs.gicd_typer &= ~GICD_TYPER_LPIS;

	/*
	 * XXX. Guest reads of GICD_PIDR2 should return the same ArchRev as
	 * specified in the guest FDT.
	 */
	ro_regs.gicd_pidr2 = gic_d_read(gic_sc, 4, GICD_PIDR2);
}

static int
arm_vgic_attach(device_t dev)
{
	vgic_v3_set_ro_regs(dev);

	return (0);
}

static void
arm_vgic_identify(driver_t *driver, device_t parent)
{
	device_t dev;

	/*
	 * After we create the VGIC device this function gets called with the
	 * VGIC as the parent. Exit in that case to avoid an infinite loop.
	 */
	if (strcmp(device_get_name(parent), VGIC_V3_DEVNAME) == 0)
		return;

	dev = device_find_child(parent, VGIC_V3_DEVNAME, -1);
	if (!dev)
		/* Create the virtual GIC device */
		dev = device_add_child(parent, VGIC_V3_DEVNAME, -1);
}

static int
arm_vgic_probe(device_t dev)
{
	device_set_desc(dev, VGIC_V3_DEVSTR);

	return (BUS_PROBE_DEFAULT);
}

static device_method_t arm_vgic_methods[] = {
	DEVMETHOD(device_identify,	arm_vgic_identify),
	DEVMETHOD(device_probe,		arm_vgic_probe),
	DEVMETHOD(device_attach,	arm_vgic_attach),
	DEVMETHOD(device_detach,	arm_vgic_detach),
	DEVMETHOD_END
};

static devclass_t arm_vgic_devclass;

DEFINE_CLASS_1(vgic, arm_vgic_driver, arm_vgic_methods, 0, gic_v3_driver);

DRIVER_MODULE(vgic, gic, arm_vgic_driver, arm_vgic_devclass, 0, 0);
