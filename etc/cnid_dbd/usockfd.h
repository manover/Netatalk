/*
 * $Id: usockfd.h,v 1.1.4.1 2003-09-09 16:42:20 didg Exp $
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


#endif /* CNID_DBD_USOCKFD_H */
