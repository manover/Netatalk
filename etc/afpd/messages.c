/*
 * $Id: messages.c,v 1.16.6.1.2.2 2003-09-11 23:36:44 bfernhomberg Exp $
 *
 * Copyright (c) 1997 Adrian Sun (asun@zoology.washington.edu)
 * All Rights Reserved.  See COPYRIGHT.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <atalk/afp.h>
#include <atalk/dsi.h>
#include <atalk/logger.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */
#include <atalk/unicode.h>
#include "globals.h"
#include "misc.h"

#ifndef MAX
#define MAX(a,b)     ((a) > (b) ? (a) : (b))
#endif /* ! MAX */

#define MAXMESGSIZE 199

/* this is only used by afpd children, so it's okay. */
static char servermesg[MAXPATHLEN] = "";
static char localized_message[MAXPATHLEN] = "";

void setmessage(const char *message)
{
    strncpy(servermesg, message, MAXMESGSIZE);
}

void readmessage(obj)
AFPObj *obj;
{
    /* Read server message from file defined as SERVERTEXT */
#ifdef SERVERTEXT
    FILE *message;
    char * filename;
    unsigned int i; 
    int rc;
    static int c;
    uid_t euid;
    u_int32_t maxmsgsize;

    maxmsgsize = (obj->proto == AFPPROTO_DSI)?MIN(MAX(((DSI*)obj->handle)->attn_quantum, MAXMESGSIZE),MAXPATHLEN):MAXMESGSIZE;

    i=0;
    /* Construct file name SERVERTEXT/message.[pid] */
    if ( NULL == (filename=(char*) malloc(sizeof(SERVERTEXT)+15)) ) {
	LOG(log_error, logtype_afpd, "readmessage: malloc: %s", strerror(errno) );
        return;
    }

    sprintf(filename, "%s/message.%d", SERVERTEXT, getpid());

#ifdef DEBUG
    LOG(log_debug, logtype_afpd, "Reading file %s ", filename);
#endif /* DEBUG */

    message=fopen(filename, "r");
    if (message==NULL) {
        LOG(log_info, logtype_afpd, "Unable to open file %s", filename);
        sprintf(filename, "%s/message", SERVERTEXT);
        message=fopen(filename, "r");
    }

    /* if either message.pid or message exists */
    if (message!=NULL) {
        /* added while loop to get characters and put in servermesg */
        while ((( c=fgetc(message)) != EOF) && (i < (maxmsgsize - 1))) {
            if ( c == '\n')  c = ' ';
            servermesg[i++] = c;
        }
        servermesg[i] = 0;

        /* cleanup */
        fclose(message);

        /* Save effective uid and switch to root to delete file. */
        /* Delete will probably fail otherwise, but let's try anyways */
        euid = geteuid();
        if (seteuid(0) < 0) {
            LOG(log_error, logtype_afpd, "Could not switch back to root: %s",
				strerror(errno));
        }

        if ( 0 < (rc = unlink(filename)) )
	    LOG(log_error, logtype_afpd, "File '%s' could not be deleted", strerror(errno));

        /* Drop privs again, failing this is very bad */
        if (seteuid(euid) < 0) {
            LOG(log_error, logtype_afpd, "Could not switch back to uid %d: %s", euid, strerror(errno));
        }

        if (rc < 0) {
            LOG(log_error, logtype_afpd, "Error deleting %s: %s", filename, strerror(rc));
        }
#ifdef DEBUG
        else {
            LOG(log_info, logtype_afpd, "Deleted %s", filename);
        }

        LOG(log_info, logtype_afpd, "Set server message to \"%s\"", servermesg);
#endif /* DEBUG */
    }
    free(filename);
#endif /* SERVERTEXT */
}

int afp_getsrvrmesg(obj, ibuf, ibuflen, rbuf, rbuflen)
AFPObj *obj;
char *ibuf, *rbuf;
int ibuflen, *rbuflen;
{
    char *message;
    u_int16_t type, bitmap;
    u_int32_t msgsize;
    size_t outlen;

    msgsize = (obj->proto == AFPPROTO_DSI)?MAX(((DSI*)obj->handle)->attn_quantum, MAXMESGSIZE):MAXMESGSIZE;

    memcpy(&type, ibuf + 2, sizeof(type));
    memcpy(&bitmap, ibuf + 4, sizeof(bitmap));

    switch (ntohs(type)) {
    case AFPMESG_LOGIN: /* login */
        message = obj->options.loginmesg;
        break;
    case AFPMESG_SERVER: /* server */
        message = servermesg;
        break;
    default:
        *rbuflen = 0;
        return AFPERR_BITMAP;
    }

    /* output format:
     * message type:   2 bytes
     * bitmap:         2 bytes
     * message length: 1 byte
     * message:        up to 199 bytes
     */
    memcpy(rbuf, &type, sizeof(type));
    rbuf += sizeof(type);
    memcpy(rbuf, &bitmap, sizeof(bitmap));
    rbuf += sizeof(bitmap);
    *rbuflen = strlen(message);
#if 0
    if (*rbuflen > MAXMESGSIZE)
        *rbuflen = MAXMESGSIZE;
#endif
    if (*rbuflen > msgsize)
        *rbuflen = msgsize;
    *rbuf++ = *rbuflen;

    /* Convert the message to the macs codepage 
     * according to AFP 3.1 specs page 200 
     * bit 1 set in bitmap means Unicode ?= utf8
     * Never saw this in the wild yet             */	

    if ( (size_t)-1 == (outlen = convert_string(obj->options.unixcharset, ((ntohs(bitmap)) & 2)?CH_UTF8_MAC:obj->options.maccharset, message, *rbuflen, localized_message, msgsize)) )
    	memcpy(rbuf, message, *rbuflen); /*FIXME*/
    else
	memcpy(rbuf, localized_message, outlen);

    *rbuflen += 5;

    return AFP_OK;
}
