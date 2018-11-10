# $FreeBSD$

PROG=	bhyveload
SRCS=	bhyveload.c
MAN=	bhyveload.8
PACKAGE=	bhyve

LIBADD=	vmmapi

WARNS?=	3

CFLAGS+=-I${SRCTOP}/stand/userboot -I${SRCTOP}/lib/libvmmapi

.include <bsd.prog.mk>
