/*
 * $Id: dsi_read.c,v 1.3.14.1 2003-10-17 00:01:14 didg Exp $
 *
 * Copyright (c) 1997 Adrian Sun (asun@zoology.washington.edu)
 * All rights reserved. See COPYRIGHT.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include <stdio.h>
#include <string.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */
#include <signal.h>
#include <sys/types.h>
#include <sys/time.h>

#include <atalk/dsi.h>

#ifndef min
#define min(a,b)   ((a) < (b) ? (a) : (b))
#endif /* ! min */

/* streaming i/o for afp_read. this is all from the perspective of the
 * client. it basically does the reverse of dsi_write. on first entry,
 * it will send off the header plus whatever is in its command
 * buffer. it returns the amount of stuff still to be read
 * (constrained by the buffer size). */
ssize_t dsi_readinit(DSI *dsi, void *buf, const size_t buflen,
		    const size_t size, const int err)
{
  const struct itimerval none = {{0, 0}, {0, 0}};

  dsi->noreply = 1; /* we will handle our own replies */
  dsi->header.dsi_flags = DSIFL_REPLY;
  /*dsi->header.dsi_command = DSIFUNC_CMD;*/
  dsi->header.dsi_len = htonl(size);
  dsi->header.dsi_code = htonl(err);

  sigprocmask(SIG_BLOCK, &dsi->sigblockset, &dsi->oldset);
  dsi->sigblocked = 1;
  setitimer(ITIMER_REAL, &none, &dsi->savetimer);
  
  if (dsi_stream_send(dsi, buf, buflen)) {
    dsi->datasize = size - buflen;
    return min(dsi->datasize, buflen);
  }

  return -1; /* error */
}

void dsi_readdone(DSI *dsi)
{
  setitimer(ITIMER_REAL, &dsi->savetimer, NULL);
  sigprocmask(SIG_SETMASK, &dsi->oldset, NULL);
  dsi->sigblocked = 0;
}

/* send off the data */
ssize_t dsi_read(DSI *dsi, void *buf, const size_t buflen)
{
  size_t len;
  int delay = (dsi->datasize != buflen)?1:0;
  
  len  = dsi_stream_write(dsi, buf, buflen, delay);

  if (len == buflen) {
    dsi->datasize -= len;
    return min(dsi->datasize, buflen);
  }

  return -1;
}
