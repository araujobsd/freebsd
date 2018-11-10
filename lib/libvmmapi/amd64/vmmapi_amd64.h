/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011 NetApp, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY NETAPP, INC ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL NETAPP, INC OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 * Note the missing header guards. This is because this file isn't intended to
 * be included as a standalone header. Use '#include <vmmapi.h>' instead.
 */

enum x2apic_state;

/*
 * Identifiers for memory segments:
 * - vm_setup_memory() uses VM_SYSMEM for the system memory segment.
 * - the remaining identifiers can be used to create devmem segments.
 */
enum {
	VM_SYSMEM,
	VM_BOOTROM,
	VM_FRAMEBUFFER,
};

/*
 * Get the length and name of the memory segment identified by 'segid'.
 * Note that system memory segments are identified with a nul name.
 *
 * Returns 0 on success and non-zero otherwise.
 */
int	vm_get_memseg(struct vmctx *ctx, int ident, size_t *lenp, char *name,
	    size_t namesiz);

/*
 * Iterate over the guest address space. This function finds an address range
 * that starts at an address >= *gpa.
 *
 * Returns 0 if the next address range was found and non-zero otherwise.
 */
int	vm_mmap_getnext(struct vmctx *ctx, vm_paddr_t *gpa, int *segid,
	    vm_ooffset_t *segoff, size_t *len, int *prot, int *flags);
/*
 * Create a device memory segment identified by 'segid'.
 *
 * Returns a pointer to the memory segment on success and MAP_FAILED otherwise.
 */
void	*vm_create_devmem(struct vmctx *ctx, int segid, const char *name,
	    size_t len);

/*
 * Map the memory segment identified by 'segid' into the guest address space
 * at [gpa,gpa+len) with protection 'prot'.
 */
int	vm_mmap_memseg(struct vmctx *ctx, vm_paddr_t gpa, int segid,
	    vm_ooffset_t segoff, size_t len, int prot);

uint32_t vm_get_lowmem_limit(struct vmctx *ctx);
void	vm_set_lowmem_limit(struct vmctx *ctx, uint32_t limit);
size_t	vm_get_lowmem_size(struct vmctx *ctx);
size_t	vm_get_highmem_size(struct vmctx *ctx);
int	vm_set_desc(struct vmctx *ctx, int vcpu, int reg,
		    uint64_t base, uint32_t limit, uint32_t access);
int	vm_get_desc(struct vmctx *ctx, int vcpu, int reg,
		    uint64_t *base, uint32_t *limit, uint32_t *access);
int	vm_get_seg_desc(struct vmctx *ctx, int vcpu, int reg,
			struct seg_desc *seg_desc);
int	vm_apicid2vcpu(struct vmctx *ctx, int apicid);
int	vm_inject_exception(struct vmctx *ctx, int vcpu, int vector,
    int errcode_valid, uint32_t errcode, int restart_instruction);
int	vm_lapic_irq(struct vmctx *ctx, int vcpu, int vector);
int	vm_lapic_local_irq(struct vmctx *ctx, int vcpu, int vector);
int	vm_lapic_msi(struct vmctx *ctx, uint64_t addr, uint64_t msg);
int	vm_ioapic_assert_irq(struct vmctx *ctx, int irq);
int	vm_ioapic_deassert_irq(struct vmctx *ctx, int irq);
int	vm_ioapic_pulse_irq(struct vmctx *ctx, int irq);
int	vm_ioapic_pincount(struct vmctx *ctx, int *pincount);
int	vm_isa_assert_irq(struct vmctx *ctx, int atpic_irq, int ioapic_irq);
int	vm_isa_deassert_irq(struct vmctx *ctx, int atpic_irq, int ioapic_irq);
int	vm_isa_pulse_irq(struct vmctx *ctx, int atpic_irq, int ioapic_irq);
int	vm_isa_set_irq_trigger(struct vmctx *ctx, int atpic_irq,
	    enum vm_intr_trigger trigger);
int	vm_inject_nmi(struct vmctx *ctx, int vcpu);

int	vm_get_intinfo(struct vmctx *ctx, int vcpu, uint64_t *i1, uint64_t *i2);
int	vm_set_intinfo(struct vmctx *ctx, int vcpu, uint64_t exit_intinfo);

int	vm_get_x2apic_state(struct vmctx *ctx, int vcpu, enum x2apic_state *s);
int	vm_set_x2apic_state(struct vmctx *ctx, int vcpu, enum x2apic_state s);

int	vm_get_hpet_capabilities(struct vmctx *ctx, uint32_t *capabilities);

/* RTC */
int	vm_rtc_write(struct vmctx *ctx, int offset, uint8_t value);
int	vm_rtc_read(struct vmctx *ctx, int offset, uint8_t *retval);
int	vm_rtc_settime(struct vmctx *ctx, time_t secs);
int	vm_rtc_gettime(struct vmctx *ctx, time_t *secs);

/* Reset vcpu register state */
int	vcpu_reset(struct vmctx *ctx, int vcpu);

/*
 * FreeBSD specific APIs
 */
int	vm_setup_freebsd_registers(struct vmctx *ctx, int vcpu,
				uint64_t rip, uint64_t cr3, uint64_t gdtbase,
				uint64_t rsp);
int	vm_setup_freebsd_registers_i386(struct vmctx *vmctx, int vcpu,
					uint32_t eip, uint32_t gdtbase,
					uint32_t esp);
void	vm_setup_freebsd_gdt(uint64_t *gdtr);
