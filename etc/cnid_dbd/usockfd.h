/*
 * $Id: usockfd.h,v 1.1.4.2 2003-10-30 10:03:19 bfernhomberg Exp $
 *
 * Copyright (C) Joerg Lenneis 2003
 * All Rights Reserved.  See COPYRIGHT.
 */

#ifndef CNID_DBD_USOCKFD_H
#define CNID_DBD_USOCKFD_H 1



#include <atalk/cnid_dbd_private.h>


extern int      usockfd_create  __P((char *, mode_t, int));
extern int      tsockfd_create  __P((char *, int, int));
extern int      usockfd_check   __P((int, unsigned long));

#ifndef OSSH_ALIGNBYTES
#define OSSH_ALIGNBYTES (sizeof(int) - 1)
#endif
#ifndef __CMSG_ALIGN
#define __CMSG_ALIGN(p) (((u_int)(p) + OSSH_ALIGNBYTES) &~ OSSH_ALIGNBYTES)
#endif

/* Length of the contents of a control message of length len */
#ifndef CMSG_LEN
#define CMSG_LEN(len)   (__CMSG_ALIGN(sizeof(struct cmsghdr)) + (len))
#endif

/* Length of the space taken up by a padded control message of length len */
#ifndef CMSG_SPACE
#define CMSG_SPACE(len) (__CMSG_ALIGN(sizeof(struct cmsghdr)) + __CMSG_ALIGN(len))
#endif



#endif /* CNID_DBD_USOCKFD_H */
