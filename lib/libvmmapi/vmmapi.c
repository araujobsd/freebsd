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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/_iovec.h>
#include <sys/cpuset.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include <libutil.h>

#include <machine/vmm.h>
#include <machine/vmm_dev.h>

#include "vmmapi.h"

#define	MB	(1024 * 1024UL)

#define	CREATE(x)  sysctlbyname("hw.vmm.create", NULL, NULL, (x), strlen((x)))
#define	DESTROY(x) sysctlbyname("hw.vmm.destroy", NULL, NULL, (x), strlen((x)))

int
vm_create(const char *name)
{

	return (CREATE((char *)name));
}

void
vm_destroy(struct vmctx *vm)
{
	int fd;

	assert(vm != NULL);

	fd = vm_get_device_fd(vm);
	if (fd >= 0)
		close(fd);
	DESTROY(vm_get_name(vm));

	free(vm);
}

int
vm_parse_memsize(const char *optarg, size_t *ret_memsize)
{
	char *endptr;
	size_t optval;
	int error;

	optval = strtoul(optarg, &endptr, 0);
	if (*optarg != '\0' && *endptr == '\0') {
		/*
		 * For the sake of backward compatibility if the memory size
		 * specified on the command line is less than a megabyte then
		 * it is interpreted as being in units of MB.
		 */
		if (optval < MB)
			optval *= MB;
		*ret_memsize = optval;
		error = 0;
	} else
		error = expand_number(optarg, ret_memsize);

	return (error);
}

int
vm_set_register(struct vmctx *ctx, int vcpu, int reg, uint64_t val)
{
	int error;
	struct vm_register vmreg;

	bzero(&vmreg, sizeof(vmreg));
	vmreg.cpuid = vcpu;
	vmreg.regnum = reg;
	vmreg.regval = val;

	error = ioctl(vm_get_device_fd(ctx), VM_SET_REGISTER, &vmreg);
	return (error);
}

int
vm_get_register(struct vmctx *ctx, int vcpu, int reg, uint64_t *ret_val)
{
	int error;
	struct vm_register vmreg;

	bzero(&vmreg, sizeof(vmreg));
	vmreg.cpuid = vcpu;
	vmreg.regnum = reg;

	error = ioctl(vm_get_device_fd(ctx), VM_GET_REGISTER, &vmreg);
	*ret_val = vmreg.regval;
	return (error);
}

int
vm_set_register_set(struct vmctx *ctx, int vcpu, unsigned int count,
    const int *regnums, uint64_t *regvals)
{
	int error;
	struct vm_register_set vmregset;

	bzero(&vmregset, sizeof(vmregset));
	vmregset.cpuid = vcpu;
	vmregset.count = count;
	vmregset.regnums = regnums;
	vmregset.regvals = regvals;

	error = ioctl(vm_get_device_fd(ctx), VM_SET_REGISTER_SET, &vmregset);
	return (error);
}

int
vm_get_register_set(struct vmctx *ctx, int vcpu, unsigned int count,
    const int *regnums, uint64_t *regvals)
{
	int error;
	struct vm_register_set vmregset;

	bzero(&vmregset, sizeof(vmregset));
	vmregset.cpuid = vcpu;
	vmregset.count = count;
	vmregset.regnums = regnums;
	vmregset.regvals = regvals;

	error = ioctl(vm_get_device_fd(ctx), VM_GET_REGISTER_SET, &vmregset);
	return (error);
}

int
vm_run(struct vmctx *ctx, int vcpu, struct vm_exit *vmexit)
{
	int error;
	struct vm_run vmrun;

	bzero(&vmrun, sizeof(vmrun));
	vmrun.cpuid = vcpu;

	error = ioctl(vm_get_device_fd(ctx), VM_RUN, &vmrun);
	bcopy(&vmrun.vm_exit, vmexit, sizeof(struct vm_exit));
	return (error);
}

int
vm_suspend(struct vmctx *ctx, enum vm_suspend_how how)
{
	struct vm_suspend vmsuspend;

	bzero(&vmsuspend, sizeof(vmsuspend));
	vmsuspend.how = how;
	return (ioctl(vm_get_device_fd(ctx), VM_SUSPEND, &vmsuspend));
}

int
vm_reinit(struct vmctx *ctx)
{

	return (ioctl(vm_get_device_fd(ctx), VM_REINIT, 0));
}

int
vm_get_capability(struct vmctx *ctx, int vcpu, enum vm_cap_type cap,
		  int *retval)
{
	int error;
	struct vm_capability vmcap;

	bzero(&vmcap, sizeof(vmcap));
	vmcap.cpuid = vcpu;
	vmcap.captype = cap;

	error = ioctl(vm_get_device_fd(ctx), VM_GET_CAPABILITY, &vmcap);
	*retval = vmcap.capval;
	return (error);
}

int
vm_set_capability(struct vmctx *ctx, int vcpu, enum vm_cap_type cap, int val)
{
	struct vm_capability vmcap;

	bzero(&vmcap, sizeof(vmcap));
	vmcap.cpuid = vcpu;
	vmcap.captype = cap;
	vmcap.capval = val;

	return (ioctl(vm_get_device_fd(ctx), VM_SET_CAPABILITY, &vmcap));
}

int
vm_assign_pptdev(struct vmctx *ctx, int bus, int slot, int func)
{
	struct vm_pptdev pptdev;

	bzero(&pptdev, sizeof(pptdev));
	pptdev.bus = bus;
	pptdev.slot = slot;
	pptdev.func = func;

	return (ioctl(vm_get_device_fd(ctx), VM_BIND_PPTDEV, &pptdev));
}

int
vm_unassign_pptdev(struct vmctx *ctx, int bus, int slot, int func)
{
	struct vm_pptdev pptdev;

	bzero(&pptdev, sizeof(pptdev));
	pptdev.bus = bus;
	pptdev.slot = slot;
	pptdev.func = func;

	return (ioctl(vm_get_device_fd(ctx), VM_UNBIND_PPTDEV, &pptdev));
}

int
vm_map_pptdev_mmio(struct vmctx *ctx, int bus, int slot, int func,
		   vm_paddr_t gpa, size_t len, vm_paddr_t hpa)
{
	struct vm_pptdev_mmio pptmmio;

	bzero(&pptmmio, sizeof(pptmmio));
	pptmmio.bus = bus;
	pptmmio.slot = slot;
	pptmmio.func = func;
	pptmmio.gpa = gpa;
	pptmmio.len = len;
	pptmmio.hpa = hpa;

	return (ioctl(vm_get_device_fd(ctx), VM_MAP_PPTDEV_MMIO, &pptmmio));
}

int
vm_setup_pptdev_msi(struct vmctx *ctx, int vcpu, int bus, int slot, int func,
    uint64_t addr, uint64_t msg, int numvec)
{
	struct vm_pptdev_msi pptmsi;

	bzero(&pptmsi, sizeof(pptmsi));
	pptmsi.vcpu = vcpu;
	pptmsi.bus = bus;
	pptmsi.slot = slot;
	pptmsi.func = func;
	pptmsi.msg = msg;
	pptmsi.addr = addr;
	pptmsi.numvec = numvec;

	return (ioctl(vm_get_device_fd(ctx), VM_PPTDEV_MSI, &pptmsi));
}

int	
vm_setup_pptdev_msix(struct vmctx *ctx, int vcpu, int bus, int slot, int func,
    int idx, uint64_t addr, uint64_t msg, uint32_t vector_control)
{
	struct vm_pptdev_msix pptmsix;

	bzero(&pptmsix, sizeof(pptmsix));
	pptmsix.vcpu = vcpu;
	pptmsix.bus = bus;
	pptmsix.slot = slot;
	pptmsix.func = func;
	pptmsix.idx = idx;
	pptmsix.msg = msg;
	pptmsix.addr = addr;
	pptmsix.vector_control = vector_control;

	return ioctl(vm_get_device_fd(ctx), VM_PPTDEV_MSIX, &pptmsix);
}

uint64_t *
vm_get_stats(struct vmctx *ctx, int vcpu, struct timeval *ret_tv,
	     int *ret_entries)
{
	int error;

	static struct vm_stats vmstats;

	vmstats.cpuid = vcpu;

	error = ioctl(vm_get_device_fd(ctx), VM_STATS, &vmstats);
	if (error == 0) {
		if (ret_entries)
			*ret_entries = vmstats.num_entries;
		if (ret_tv)
			*ret_tv = vmstats.tv;
		return (vmstats.statbuf);
	} else
		return (NULL);
}

const char *
vm_get_stat_desc(struct vmctx *ctx, int index)
{
	static struct vm_stat_desc statdesc;

	statdesc.index = index;
	if (ioctl(vm_get_device_fd(ctx), VM_STAT_DESC, &statdesc) == 0)
		return (statdesc.desc);
	else
		return (NULL);
}

int
vm_get_gpa_pmap(struct vmctx *ctx, uint64_t gpa, uint64_t *pte, int *num)
{
	int error, i;
	struct vm_gpa_pte gpapte;

	bzero(&gpapte, sizeof(gpapte));
	gpapte.gpa = gpa;

	error = ioctl(vm_get_device_fd(ctx), VM_GET_GPA_PMAP, &gpapte);

	if (error == 0) {
		*num = gpapte.ptenum;
		for (i = 0; i < gpapte.ptenum; i++)
			pte[i] = gpapte.pte[i];
	}

	return (error);
}

int
vm_gla2gpa(struct vmctx *ctx, int vcpu, struct vm_guest_paging *paging,
    uint64_t gla, int prot, uint64_t *gpa, int *fault)
{
	struct vm_gla2gpa gg;
	int error;

	bzero(&gg, sizeof(struct vm_gla2gpa));
	gg.vcpuid = vcpu;
	gg.prot = prot;
	gg.gla = gla;
	gg.paging = *paging;

	error = ioctl(vm_get_device_fd(ctx), VM_GLA2GPA, &gg);
	if (error == 0) {
		*fault = gg.fault;
		*gpa = gg.gpa;
	}
	return (error);
}

int
vm_gla2gpa_nofault(struct vmctx *ctx, int vcpu, struct vm_guest_paging *paging,
    uint64_t gla, int prot, uint64_t *gpa, int *fault)
{
	struct vm_gla2gpa gg;
	int error;

	bzero(&gg, sizeof(struct vm_gla2gpa));
	gg.vcpuid = vcpu;
	gg.prot = prot;
	gg.gla = gla;
	gg.paging = *paging;

	error = ioctl(vm_get_device_fd(ctx), VM_GLA2GPA_NOFAULT, &gg);
	if (error == 0) {
		*fault = gg.fault;
		*gpa = gg.gpa;
	}
	return (error);
}

#ifndef min
#define	min(a,b)	(((a) < (b)) ? (a) : (b))
#endif

int
vm_copy_setup(struct vmctx *ctx, int vcpu, struct vm_guest_paging *paging,
    uint64_t gla, size_t len, int prot, struct iovec *iov, int iovcnt,
    int *fault)
{
	void *va;
	uint64_t gpa;
	int error, i, n, off;

	for (i = 0; i < iovcnt; i++) {
		iov[i].iov_base = 0;
		iov[i].iov_len = 0;
	}

	while (len) {
		assert(iovcnt > 0);
		error = vm_gla2gpa(ctx, vcpu, paging, gla, prot, &gpa, fault);
		if (error || *fault)
			return (error);

		off = gpa & PAGE_MASK;
		n = min(len, PAGE_SIZE - off);

		va = vm_map_gpa(ctx, gpa, n);
		if (va == NULL)
			return (EFAULT);

		iov->iov_base = va;
		iov->iov_len = n;
		iov++;
		iovcnt--;

		gla += n;
		len -= n;
	}
	return (0);
}

void
vm_copy_teardown(struct vmctx *ctx, int vcpu, struct iovec *iov, int iovcnt)
{

	return;
}

void
vm_copyin(struct vmctx *ctx, int vcpu, struct iovec *iov, void *vp, size_t len)
{
	const char *src;
	char *dst;
	size_t n;

	dst = vp;
	while (len) {
		assert(iov->iov_len);
		n = min(len, iov->iov_len);
		src = iov->iov_base;
		bcopy(src, dst, n);

		iov++;
		dst += n;
		len -= n;
	}
}

void
vm_copyout(struct vmctx *ctx, int vcpu, const void *vp, struct iovec *iov,
    size_t len)
{
	const char *src;
	char *dst;
	size_t n;

	src = vp;
	while (len) {
		assert(iov->iov_len);
		n = min(len, iov->iov_len);
		dst = iov->iov_base;
		bcopy(src, dst, n);

		iov++;
		src += n;
		len -= n;
	}
}

static int
vm_get_cpus(struct vmctx *ctx, int which, cpuset_t *cpus)
{
	struct vm_cpuset vm_cpuset;
	int error;

	bzero(&vm_cpuset, sizeof(struct vm_cpuset));
	vm_cpuset.which = which;
	vm_cpuset.cpusetsize = sizeof(cpuset_t);
	vm_cpuset.cpus = cpus;

	error = ioctl(vm_get_device_fd(ctx), VM_GET_CPUS, &vm_cpuset);
	return (error);
}

int
vm_active_cpus(struct vmctx *ctx, cpuset_t *cpus)
{

	return (vm_get_cpus(ctx, VM_ACTIVE_CPUS, cpus));
}

int
vm_suspended_cpus(struct vmctx *ctx, cpuset_t *cpus)
{

	return (vm_get_cpus(ctx, VM_SUSPENDED_CPUS, cpus));
}

int
vm_debug_cpus(struct vmctx *ctx, cpuset_t *cpus)
{

	return (vm_get_cpus(ctx, VM_DEBUG_CPUS, cpus));
}

int
vm_activate_cpu(struct vmctx *ctx, int vcpu)
{
	struct vm_activate_cpu ac;
	int error;

	bzero(&ac, sizeof(struct vm_activate_cpu));
	ac.vcpuid = vcpu;
	error = ioctl(vm_get_device_fd(ctx), VM_ACTIVATE_CPU, &ac);
	return (error);
}

int
vm_suspend_cpu(struct vmctx *ctx, int vcpu)
{
	struct vm_activate_cpu ac;
	int error;

	bzero(&ac, sizeof(struct vm_activate_cpu));
	ac.vcpuid = vcpu;
	error = ioctl(vm_get_device_fd(ctx), VM_SUSPEND_CPU, &ac);
	return (error);
}

int
vm_resume_cpu(struct vmctx *ctx, int vcpu)
{
	struct vm_activate_cpu ac;
	int error;

	bzero(&ac, sizeof(struct vm_activate_cpu));
	ac.vcpuid = vcpu;
	error = ioctl(vm_get_device_fd(ctx), VM_RESUME_CPU, &ac);
	return (error);
}

int
vm_restart_instruction(void *arg, int vcpu)
{
	struct vmctx *ctx = arg;

	return (ioctl(vm_get_device_fd(ctx), VM_RESTART_INSTRUCTION, &vcpu));
}

int
vm_set_topology(struct vmctx *ctx,
    uint16_t sockets, uint16_t cores, uint16_t threads, uint16_t maxcpus)
{
	struct vm_cpu_topology topology;

	bzero(&topology, sizeof (struct vm_cpu_topology));
	topology.sockets = sockets;
	topology.cores = cores;
	topology.threads = threads;
	topology.maxcpus = maxcpus;
	return (ioctl(vm_get_device_fd(ctx), VM_SET_TOPOLOGY, &topology));
}

int
vm_get_topology(struct vmctx *ctx,
    uint16_t *sockets, uint16_t *cores, uint16_t *threads, uint16_t *maxcpus)
{
	struct vm_cpu_topology topology;
	int error;

	bzero(&topology, sizeof (struct vm_cpu_topology));
	error = ioctl(vm_get_device_fd(ctx), VM_GET_TOPOLOGY, &topology);
	if (error == 0) {
		*sockets = topology.sockets;
		*cores = topology.cores;
		*threads = topology.threads;
		*maxcpus = topology.maxcpus;
	}
	return (error);
}
