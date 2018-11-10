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

#ifndef _VMMAPI_H_
#define	_VMMAPI_H_

#include <sys/param.h>
#include <sys/cpuset.h>

/*
 * API version for out-of-tree consumers like grub-bhyve for making compile
 * time decisions.
 */
#define	VMMAPI_VERSION	0103	/* 2 digit major followed by 2 digit minor */

struct iovec;
struct vmctx;

/*
 * Different styles of mapping the memory assigned to a VM into the address
 * space of the controlling process.
 */
enum vm_mmap_style {
	VM_MMAP_NONE,		/* no mapping */
	VM_MMAP_ALL,		/* fully and statically mapped */
	VM_MMAP_SPARSE,		/* mappings created on-demand */
};

/*
 * 'flags' value passed to 'vm_set_memflags()'.
 */
#define	VM_MEM_F_INCORE	0x01	/* include guest memory in core file */
#define	VM_MEM_F_WIRED	0x02	/* guest memory is wired */

int	vm_create(const char *name);
int	vm_get_device_fd(struct vmctx *ctx);
struct vmctx *vm_open(const char *name);
void	vm_destroy(struct vmctx *ctx);
int	vm_parse_memsize(const char *optarg, size_t *memsize);
int	vm_setup_memory(struct vmctx *ctx, size_t len, enum vm_mmap_style s);
void	*vm_map_gpa(struct vmctx *ctx, vm_paddr_t gaddr, size_t len);
int	vm_get_gpa_pmap(struct vmctx *, uint64_t gpa, uint64_t *pte, int *num);
int	vm_gla2gpa(struct vmctx *, int vcpuid, struct vm_guest_paging *paging,
		   uint64_t gla, int prot, uint64_t *gpa, int *fault);
int	vm_gla2gpa_nofault(struct vmctx *, int vcpuid,
		   struct vm_guest_paging *paging, uint64_t gla, int prot,
		   uint64_t *gpa, int *fault);
void	vm_set_memflags(struct vmctx *ctx, int flags);
int	vm_get_memflags(struct vmctx *ctx);
int	vm_set_register(struct vmctx *ctx, int vcpu, int reg, uint64_t val);
int	vm_get_register(struct vmctx *ctx, int vcpu, int reg, uint64_t *retval);
int	vm_set_register_set(struct vmctx *ctx, int vcpu, unsigned int count,
    const int *regnums, uint64_t *regvals);
int	vm_get_register_set(struct vmctx *ctx, int vcpu, unsigned int count,
    const int *regnums, uint64_t *regvals);
int	vm_run(struct vmctx *ctx, int vcpu, struct vm_exit *ret_vmexit);
int	vm_suspend(struct vmctx *ctx, enum vm_suspend_how how);
int	vm_reinit(struct vmctx *ctx);
int	vm_capability_name2type(const char *capname);
const char *vm_capability_type2name(int type);
int	vm_get_capability(struct vmctx *ctx, int vcpu, enum vm_cap_type cap,
			  int *retval);
int	vm_set_capability(struct vmctx *ctx, int vcpu, enum vm_cap_type cap,
			  int val);
int	vm_assign_pptdev(struct vmctx *ctx, int bus, int slot, int func);
int	vm_unassign_pptdev(struct vmctx *ctx, int bus, int slot, int func);
int	vm_map_pptdev_mmio(struct vmctx *ctx, int bus, int slot, int func,
			   vm_paddr_t gpa, size_t len, vm_paddr_t hpa);
int	vm_setup_pptdev_msi(struct vmctx *ctx, int vcpu, int bus, int slot,
	    int func, uint64_t addr, uint64_t msg, int numvec);
int	vm_setup_pptdev_msix(struct vmctx *ctx, int vcpu, int bus, int slot,
	    int func, int idx, uint64_t addr, uint64_t msg,
	    uint32_t vector_control);

const cap_ioctl_t *vm_get_ioctls(size_t *len);

/*
 * Return a pointer to the statistics buffer. Note that this is not MT-safe.
 */
uint64_t *vm_get_stats(struct vmctx *ctx, int vcpu, struct timeval *ret_tv,
		       int *ret_entries);
const char *vm_get_stat_desc(struct vmctx *ctx, int index);

/*
 * Translate the GLA range [gla,gla+len) into GPA segments in 'iov'.
 * The 'iovcnt' should be big enough to accommodate all GPA segments.
 *
 * retval	fault		Interpretation
 *   0		  0		Success
 *   0		  1		An exception was injected into the guest
 * EFAULT	 N/A		Error
 */
int	vm_copy_setup(struct vmctx *ctx, int vcpu, struct vm_guest_paging *pg,
	    uint64_t gla, size_t len, int prot, struct iovec *iov, int iovcnt,
	    int *fault);
void	vm_copyin(struct vmctx *ctx, int vcpu, struct iovec *guest_iov,
	    void *host_dst, size_t len);
void	vm_copyout(struct vmctx *ctx, int vcpu, const void *host_src,
	    struct iovec *guest_iov, size_t len);
void	vm_copy_teardown(struct vmctx *ctx, int vcpu, struct iovec *iov,
	    int iovcnt);

int	vm_active_cpus(struct vmctx *ctx, cpuset_t *cpus);
int	vm_suspended_cpus(struct vmctx *ctx, cpuset_t *cpus);
int	vm_debug_cpus(struct vmctx *ctx, cpuset_t *cpus);
int	vm_activate_cpu(struct vmctx *ctx, int vcpu);
int	vm_suspend_cpu(struct vmctx *ctx, int vcpu);
int	vm_resume_cpu(struct vmctx *ctx, int vcpu);

/* CPU topology */
int	vm_set_topology(struct vmctx *ctx, uint16_t sockets, uint16_t cores,
	    uint16_t threads, uint16_t maxcpus);
int	vm_get_topology(struct vmctx *ctx, uint16_t *sockets, uint16_t *cores,
	    uint16_t *threads, uint16_t *maxcpus);

const char *vm_get_name(struct vmctx *ctx);

#if defined(__amd64__)
#include <amd64/vmmapi_amd64.h>
#endif
#endif	/* _VMMAPI_H_ */
