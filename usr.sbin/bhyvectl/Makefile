#
# $FreeBSD$
#

PROG=	bhyvectl
SRCS=	bhyvectl.c
PACKAGE=	bhyve

MAN=	bhyvectl.8

LIBADD=	vmmapi util

WARNS?=	3

CFLAGS+= -I${SRCTOP}/sys/amd64/vmm -I${SRCTOP}/lib/libvmmapi

.include <bsd.prog.mk>
