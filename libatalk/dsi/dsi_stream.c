/*
 * $Id: dsi_stream.c,v 1.11.6.3 2004-02-06 13:34:35 didg Exp $
 *
 * Copyright (c) 1998 Adrian Sun (asun@zoology.washington.edu)
 * All rights reserved. See COPYRIGHT.
 *
 * this file provides the following functions:
 * dsi_stream_write:    just write a bunch of bytes.
 * dsi_stream_read:     just read a bunch of bytes.
 * dsi_stream_send:     send a DSI header + data.
 * dsi_stream_receive:  read a DSI header + data.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#define USE_WRITEV

#include <stdio.h>
#include <stdlib.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>

#ifdef USE_WRITEV
#include <sys/uio.h>
#endif

#include <atalk/logger.h>

#include <atalk/dsi.h>
#include <netatalk/endian.h>

#define min(a,b)  ((a) < (b) ? (a) : (b))

#ifndef MSG_MORE
#define MSG_MORE 0x8000
#endif

/* write raw data. return actual bytes read. checks against EINTR
 * aren't necessary if all of the signals have SA_RESTART
 * specified. */
size_t dsi_stream_write(DSI *dsi, void *data, const size_t length, int mode)
{
  size_t written;
  ssize_t len;
#if 0
  /* FIXME sometime it's slower */
  unsigned int flags = (mode)?MSG_MORE:0;
#endif
  unsigned int flags = 0;

  written = 0;
  while (written < length) {
    if ((-1 == (len = send(dsi->socket, (u_int8_t *) data + written,
		      length - written, flags)) && errno == EINTR) ||
	!len)
      continue;

    if (len < 0) {
      LOG(log_error, logtype_default, "dsi_stream_write: %s", strerror(errno));
      break;
    }
    
    written += len;
  }

  dsi->write_count += written;
  return written;
}

/* ---------------------------------------
 * read raw data. return actual bytes read. this will wait until 
 * it gets length bytes 
 */
size_t dsi_stream_read(DSI *dsi, void *data, const size_t length)
{
  size_t stored;
  ssize_t len;
  
  stored = 0;
  while (stored < length) {
    len = read(dsi->socket, (u_int8_t *) data + stored, length - stored);
    if (len == -1 && errno == EINTR)
      continue;
    else if (len > 0)
      stored += len;
    else { /* eof or error */
      /* don't log EOF error if it's just after connect (OSX 10.3 probe) */
      if (len || stored || dsi->read_count) {
          LOG(log_error, logtype_default, "dsi_stream_read(%d): %s", len, (len < 0)?strerror(errno):"unexpected EOF");
      }
      break;
    }
  }

  dsi->read_count += stored;
  return stored;
}

/* ---------------------------------------
*/
void dsi_sleep(DSI *dsi, const int state)
{
    dsi->asleep = state;
}

/* ---------------------------------------
*/
static void block_sig(DSI *dsi)
{
  if (!dsi->sigblocked) sigprocmask(SIG_BLOCK, &dsi->sigblockset, &dsi->oldset);
}

/* ---------------------------------------
*/
static void unblock_sig(DSI *dsi)
{
  if (!dsi->sigblocked) sigprocmask(SIG_SETMASK, &dsi->oldset, NULL);
}

/* ---------------------------------------
 * write data. 0 on failure. this assumes that dsi_len will never
 * cause an overflow in the data buffer. 
 */
int dsi_stream_send(DSI *dsi, void *buf, size_t length)
{
  char block[DSI_BLOCKSIZ];
#ifdef USE_WRITEV
  struct iovec iov[2];
  size_t towrite;
  ssize_t len;
#endif /* USE_WRITEV */

  block[0] = dsi->header.dsi_flags;
  block[1] = dsi->header.dsi_command;
  memcpy(block + 2, &dsi->header.dsi_requestID, 
	 sizeof(dsi->header.dsi_requestID));
  memcpy(block + 4, &dsi->header.dsi_code, sizeof(dsi->header.dsi_code));
  memcpy(block + 8, &dsi->header.dsi_len, sizeof(dsi->header.dsi_len));
  memcpy(block + 12, &dsi->header.dsi_reserved,
	 sizeof(dsi->header.dsi_reserved));

  /* block signals */
  block_sig(dsi);

  if (!length) { /* just write the header */
    length = (dsi_stream_write(dsi, block, sizeof(block), 0) == sizeof(block));
    unblock_sig(dsi);
    return length; /* really 0 on failure, 1 on success */
  }
  
#ifdef USE_WRITEV
  iov[0].iov_base = block;
  iov[0].iov_len = sizeof(block);
  iov[1].iov_base = buf;
  iov[1].iov_len = length;
  
  towrite = sizeof(block) + length;
  dsi->write_count += towrite;
  while (towrite > 0) {
    if (((len = writev(dsi->socket, iov, 2)) == -1 && errno == EINTR) || 
	!len)
      continue;
    
    if (len == towrite) /* wrote everything out */
      break;
    else if (len < 0) { /* error */
      LOG(log_error, logtype_default, "dsi_stream_send: %s", strerror(errno));
      unblock_sig(dsi);
      return 0;
    }
    
    towrite -= len;
    if (towrite > length) { /* skip part of header */
      iov[0].iov_base = (char *) iov[0].iov_base + len;
      iov[0].iov_len -= len;
    } else { /* skip to data */
      if (iov[0].iov_len) {
	len -= iov[0].iov_len;
	iov[0].iov_len = 0;
      }
      iov[1].iov_base = (char *) iov[1].iov_base + len;
      iov[1].iov_len -= len;
    }
  }
  
#else /* USE_WRITEV */
  /* write the header then data */
  if ((dsi_stream_write(dsi, block, sizeof(block), 1) != sizeof(block)) ||
            (dsi_stream_write(dsi, buf, length, 0) != length)) {
      unblock_sig(dsi);
      return 0;
  }
#endif /* USE_WRITEV */

  unblock_sig(dsi);
  return 1;
}


/* ---------------------------------------
 * read data. function on success. 0 on failure. data length gets
 * stored in length variable. this should really use size_t's, but
 * that would require changes elsewhere. */
int dsi_stream_receive(DSI *dsi, void *buf, const size_t ilength,
		       size_t *rlength)
{
  char block[DSI_BLOCKSIZ];

  /* read in the header */
  if (dsi_stream_read(dsi, block, sizeof(block)) != sizeof(block)) 
    return 0;

  dsi->header.dsi_flags = block[0];
  dsi->header.dsi_command = block[1];
  /* FIXME, not the right place, 
     but we get a server disconnect without reason in the log
  */
  if (!block[1]) {
      LOG(log_error, logtype_default, "dsi_stream_receive: invalid packet, fatal");
      return 0;
  }

  memcpy(&dsi->header.dsi_requestID, block + 2, 
	 sizeof(dsi->header.dsi_requestID));
  memcpy(&dsi->header.dsi_code, block + 4, sizeof(dsi->header.dsi_code));
  memcpy(&dsi->header.dsi_len, block + 8, sizeof(dsi->header.dsi_len));
  memcpy(&dsi->header.dsi_reserved, block + 12,
	 sizeof(dsi->header.dsi_reserved));
  dsi->clientID = ntohs(dsi->header.dsi_requestID);
  
  /* make sure we don't over-write our buffers. */
  *rlength = min(ntohl(dsi->header.dsi_len), ilength);
  if (dsi_stream_read(dsi, buf, *rlength) != *rlength) 
    return 0;

  return block[1];
}
