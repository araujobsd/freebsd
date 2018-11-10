# $FreeBSD$

PACKAGE=lib${LIB}
LIB=	vmmapi
SRCS=	vmmapi.c
INCS=	vmmapi.h

WARNS?=	2

LIBADD=	util

CFLAGS+= -I${.CURDIR}

.if exists(${.CURDIR}/${MACHINE})
ARCH_SUBDIR= ${MACHINE}
.elif exists(${.CURDIR}/${MACHINE_ARCH})
ARCH_SUBDIR= ${MACHINE_ARCH}
.else
ARCH_SUBDIR= ${MACHINE_CPUARCH}
.endif

CFLAGS+= -I${.CURDIR} -I${.CURDIR}/${ARCH_SUBDIR}

.include "${ARCH_SUBDIR}/Makefile.inc"
.include <bsd.lib.mk>
