/* 
   Unix SMB/CIFS implementation.
   minimal iconv implementation
   Copyright (C) Andrew Tridgell 2001
   Copyright (C) Jelmer Vernooij 2002,2003
   
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
   
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
   
   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
   
   From samba 3.0 beta and GNU libiconv-1.8
   It's bad but most of the time we can't use libc iconv service:
   - it doesn't round trip for most encoding
   - it doesn't know about Apple extension
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <atalk/logger.h>
#include <errno.h>

#include <netatalk/endian.h>
#include <atalk/unicode.h>

#ifdef HAVE_USABLE_ICONV
#include <iconv.h>
#endif


#include "mac_roman.h"
#include "mac_hebrew.h"

/**
 * @file
 *
 * @brief Samba wrapper/stub for iconv character set conversion.
 *
 * iconv is the XPG2 interface for converting between character
 * encodings.  This file provides a Samba wrapper around it, and also
 * a simple reimplementation that is used if the system does not
 * implement iconv.
 *
 * Samba only works with encodings that are supersets of ASCII: ascii
 * characters like whitespace can be tested for directly, multibyte
 * sequences start with a byte with the high bit set, and strings are
 * terminated by a nul byte.
 *
 * Note that the only function provided by iconv is conversion between
 * characters.  It doesn't directly support operations like
 * uppercasing or comparison.  We have to convert to UCS-2 and compare
 * there.
 *
 * @sa Samba Developers Guide
 **/

static size_t ascii_pull(void *,char **, size_t *, char **, size_t *);
static size_t ascii_push(void *,char **, size_t *, char **, size_t *);
static size_t  utf8_pull(void *,char **, size_t *, char **, size_t *);
static size_t  utf8_push(void *,char **, size_t *, char **, size_t *);
static size_t iconv_copy(void *,char **, size_t *, char **, size_t *);

static size_t   mac_pull(void *,char **, size_t *, char **, size_t *);
static size_t   mac_push(void *,char **, size_t *, char **, size_t *);

static size_t   mac_hebrew_pull(void *,char **, size_t *, char **, size_t *);
static size_t   mac_hebrew_push(void *,char **, size_t *, char **, size_t *);

static struct charset_functions builtin_functions[] = {
	{"UCS-2LE",   iconv_copy, iconv_copy},
	{"UTF8",      utf8_pull,  utf8_push},
	{"UTF-8",     utf8_pull,  utf8_push},
	{"ASCII",     ascii_pull, ascii_push},
	{"MAC",       mac_pull,  mac_push},
	{"MAC-HEBR",  mac_hebrew_pull,  mac_hebrew_push},
	{NULL, NULL, NULL}
};

#define DLIST_ADD(list, p) \
{ \
        if (!(list)) { \
                (list) = (p); \
                (p)->next = (p)->prev = NULL; \
        } else { \
                (list)->prev = (p); \
                (p)->next = (list); \
                (p)->prev = NULL; \
                (list) = (p); \
        }\
}



static struct charset_functions *charsets = NULL;

static struct charset_functions *find_charset_functions(const char *name) 
{
	struct charset_functions *c = charsets;

	while(c) {
		if (strcasecmp(name, c->name) == 0) {
			return c;
		}
		c = c->next;
	}

	return NULL;
}

int atalk_register_charset(struct charset_functions *funcs) 
{
	if (!funcs) {
		return -1;
	}

	LOG(log_debug, logtype_default, "Attempting to register new charset %s", funcs->name);
	/* Check whether we already have this charset... */
	if (find_charset_functions(funcs->name)) {
		LOG (log_debug, logtype_default, "Duplicate charset %s, not registering", funcs->name);
		return -2;
	}

	funcs->next = funcs->prev = NULL;
	LOG(log_debug, logtype_default, "Registered charset %s", funcs->name);
	DLIST_ADD(charsets, funcs);
	return 0;
}

void lazy_initialize_iconv(void)
{
	static int initialized = 0;
	int i;

	if (!initialized) {
		initialized = 1;
		for(i = 0; builtin_functions[i].name; i++) 
			atalk_register_charset(&builtin_functions[i]);
	}
}

/* if there was an error then reset the internal state,
   this ensures that we don't have a shift state remaining for
   character sets like SJIS */
static size_t sys_iconv(void *cd, 
			char **inbuf, size_t *inbytesleft,
			char **outbuf, size_t *outbytesleft)
{
#ifdef HAVE_USABLE_ICONV
	size_t ret = iconv((iconv_t)cd, 
			   inbuf, inbytesleft, 
			   outbuf, outbytesleft);
	if (ret == (size_t)-1) iconv(cd, NULL, NULL, NULL, NULL);
	return ret;
#else
	errno = EINVAL;
	return -1;
#endif
}

/**
 * This is a simple portable iconv() implementaion.
 *
 * It only knows about a very small number of character sets - just
 * enough that netatalk works on systems that don't have iconv.
 **/
size_t atalk_iconv(atalk_iconv_t cd, 
		 const char **inbuf, size_t *inbytesleft,
		 char **outbuf, size_t *outbytesleft)
{
	char cvtbuf[2048];
	char *bufp = cvtbuf;
	size_t bufsize;

	/* in many cases we can go direct */
	if (cd->direct) {
		return cd->direct(cd->cd_direct, 
				  (char **)inbuf, inbytesleft, outbuf, outbytesleft);
	}


	/* otherwise we have to do it chunks at a time */
	while (*inbytesleft > 0) {
		bufp = cvtbuf;
		bufsize = sizeof(cvtbuf);
		
		if (cd->pull(cd->cd_pull, (char **)inbuf, inbytesleft, &bufp, &bufsize) == (size_t)-1
		       && errno != E2BIG) {
		    return -1;
		}

		bufp = cvtbuf;
		bufsize = sizeof(cvtbuf) - bufsize;

		if (cd->push(cd->cd_push, &bufp, &bufsize, outbuf, outbytesleft) == (size_t)-1) {
		    return -1;
		}
	}

	return 0;
}


size_t atalk_iconv_ignore(atalk_iconv_t cd, 
		 const char **inbuf, size_t *inbytesleft,
		 char **outbuf, size_t *outbytesleft, int *ignore)
{
	char cvtbuf[2048];
	char *bufp = cvtbuf;
	size_t bufsize;
	size_t outlen = *outbytesleft;
	char *o_save;
	
	/* we have to do it chunks at a time */
	while (*inbytesleft > 0) {
		bufp = cvtbuf;
		bufsize = sizeof(cvtbuf);
		
		if (cd->pull(cd->cd_pull, (char **)inbuf, inbytesleft, &bufp, &bufsize) == (size_t)-1
		        && errno != E2BIG) {
		    return -1;
		}

		bufp = cvtbuf;
		bufsize = sizeof(cvtbuf) - bufsize;

		o_save = *outbuf;
convert_push:
		if (cd->push(cd->cd_push, 
			     &bufp, &bufsize, 
			     outbuf, outbytesleft) == (size_t)-1) {
		    if (errno == EILSEQ) {
			o_save[outlen-*outbytesleft] = '_';
			(*outbuf) = o_save + outlen-*outbytesleft+1;
			(*outbytesleft) -=1;
			bufp += 2;
			bufsize -= 2;
			//outlen=*outbytesleft;
			*ignore = 1;
			goto convert_push;
		    }
		    else
			return (size_t)(-1);
		}
	}
	return 0;
}

/*
  simple iconv_open() wrapper
 */
atalk_iconv_t atalk_iconv_open(const char *tocode, const char *fromcode)
{
	atalk_iconv_t ret;
	struct charset_functions *from, *to;


	lazy_initialize_iconv();
	from = charsets;
	to = charsets;

	ret = (atalk_iconv_t)malloc(sizeof(*ret));
	if (!ret) {
		errno = ENOMEM;
		return (atalk_iconv_t)-1;
	}
	memset(ret, 0, sizeof(*ret));

	ret->from_name = strdup(fromcode);
	ret->to_name = strdup(tocode);

	/* check for the simplest null conversion */
	if (strcasecmp(fromcode, tocode) == 0) {
		ret->direct = iconv_copy;
		return ret;
	}

	/* check if we have a builtin function for this conversion */
	from = find_charset_functions(fromcode);
	if(from)ret->pull = from->pull;
	
	to = find_charset_functions(tocode);
	if(to)ret->push = to->push;

	/* check if we can use iconv for this conversion */
#ifdef HAVE_USABLE_ICONV
	if (!ret->pull) {
		ret->cd_pull = iconv_open("UCS-2LE", fromcode);
		if (ret->cd_pull != (iconv_t)-1)
			ret->pull = sys_iconv;
	}

	if (!ret->push) {
		ret->cd_push = iconv_open(tocode, "UCS-2LE");
		if (ret->cd_push != (iconv_t)-1)
			ret->push = sys_iconv;
	}
#endif
	
	if (!ret->push || !ret->pull) {
		SAFE_FREE(ret->from_name);
		SAFE_FREE(ret->to_name);
		SAFE_FREE(ret);
		errno = EINVAL;
		return (atalk_iconv_t)-1;
	}

	/* check for conversion to/from ucs2 */
	if (strcasecmp(fromcode, "UCS-2LE") == 0 && to) {
		ret->direct = to->push;
		ret->push = ret->pull = NULL;
		return ret;
	}

	if (strcasecmp(tocode, "UCS-2LE") == 0 && from) {
		ret->direct = from->pull;
		ret->push = ret->pull = NULL;
		return ret;
	}

	/* Check if we can do the conversion direct */
#ifdef HAVE_USABLE_ICONV
	if (strcasecmp(fromcode, "UCS-2LE") == 0) {
		ret->direct = sys_iconv;
		ret->cd_direct = ret->cd_push;
		ret->cd_push = NULL;
		return ret;
	}
	if (strcasecmp(tocode, "UCS-2LE") == 0) {
		ret->direct = sys_iconv;
		ret->cd_direct = ret->cd_pull;
		ret->cd_pull = NULL;
		return ret;
	}
#endif

	return ret;
}

/*
  simple iconv_close() wrapper
*/
int atalk_iconv_close (atalk_iconv_t cd)
{
#ifdef HAVE_USABLE_ICONV
	if (cd->cd_direct) iconv_close((iconv_t)cd->cd_direct);
	if (cd->cd_pull) iconv_close((iconv_t)cd->cd_pull);
	if (cd->cd_push) iconv_close((iconv_t)cd->cd_push);
#endif

	SAFE_FREE(cd->from_name);
	SAFE_FREE(cd->to_name);

	memset(cd, 0, sizeof(*cd));
	SAFE_FREE(cd);
	return 0;
}


/************************************************************************
 the following functions implement the builtin character sets in Netatalk
*************************************************************************/

static size_t ascii_pull(void *cd, char **inbuf, size_t *inbytesleft,
			 char **outbuf, size_t *outbytesleft)
{
	while (*inbytesleft >= 1 && *outbytesleft >= 2) {
		(*outbuf)[0] = (*inbuf)[0];
		(*outbuf)[1] = 0;
		(*inbytesleft)  -= 1;
		(*outbytesleft) -= 2;
		(*inbuf)  += 1;
		(*outbuf) += 2;
	}

	if (*inbytesleft > 0) {
		errno = E2BIG;
		return -1;
	}
	
	return 0;
}

static size_t ascii_push(void *cd, char **inbuf, size_t *inbytesleft,
			 char **outbuf, size_t *outbytesleft)
{
	int ir_count=0;

	while (*inbytesleft >= 2 && *outbytesleft >= 1) {
		(*outbuf)[0] = (*inbuf)[0] & 0x7F;
		if ((*inbuf)[1]) ir_count++;
		(*inbytesleft)  -= 2;
		(*outbytesleft) -= 1;
		(*inbuf)  += 2;
		(*outbuf) += 1;
	}

	if (*inbytesleft == 1) {
		errno = EINVAL;
		return -1;
	}

	if (*inbytesleft > 1) {
		errno = E2BIG;
		return -1;
	}
	
	return ir_count;
}


static size_t iconv_copy(void *cd, char **inbuf, size_t *inbytesleft,
			 char **outbuf, size_t *outbytesleft)
{
	int n;

	n = MIN(*inbytesleft, *outbytesleft);

	memmove(*outbuf, *inbuf, n);

	(*inbytesleft) -= n;
	(*outbytesleft) -= n;
	(*inbuf) += n;
	(*outbuf) += n;

	if (*inbytesleft > 0) {
		errno = E2BIG;
		return -1;
	}

	return 0;
}

/* ------------------------ */
static size_t utf8_pull(void *cd, char **inbuf, size_t *inbytesleft,
			 char **outbuf, size_t *outbytesleft)
{
	while (*inbytesleft >= 1 && *outbytesleft >= 2) {
		unsigned char *c = (unsigned char *)*inbuf;
		unsigned char *uc = (unsigned char *)*outbuf;
		int len = 1;

		if ((c[0] & 0x80) == 0) {
			uc[0] = c[0];
			uc[1] = 0;
		} else if ((c[0] & 0xf0) == 0xe0) {
			if (*inbytesleft < 3) {
				LOG(log_debug, logtype_default, "short utf8 char\n");
				goto badseq;
			}
			uc[1] = ((c[0]&0xF)<<4) | ((c[1]>>2)&0xF);
			uc[0] = (c[1]<<6) | (c[2]&0x3f);
			len = 3;
		} else if ((c[0] & 0xe0) == 0xc0) {
			if (*inbytesleft < 2) {
				LOG(log_debug, logtype_default, "short utf8 char\n");
				goto badseq;
			}
			uc[1] = (c[0]>>2) & 0x7;
			uc[0] = (c[0]<<6) | (c[1]&0x3f);
			len = 2;
		}

		(*inbuf)  += len;
		(*inbytesleft)  -= len;
		(*outbytesleft) -= 2;
		(*outbuf) += 2;
	}

	if (*inbytesleft > 0) {
		errno = E2BIG;
		return -1;
	}
	
	return 0;

badseq:
	errno = EINVAL;
	return -1;
}

/* ------------------------ */
static size_t utf8_push(void *cd, char **inbuf, size_t *inbytesleft,
			 char **outbuf, size_t *outbytesleft)
{
	while (*inbytesleft >= 2 && *outbytesleft >= 1) {
		unsigned char *c = (unsigned char *)*outbuf;
		unsigned char *uc = (unsigned char *)*inbuf;
		int len=1;

		if (uc[1] & 0xf8) {
			if (*outbytesleft < 3) {
				LOG(log_debug, logtype_default, "short utf8 write\n");
				goto toobig;
			}
			c[0] = 0xe0 | (uc[1]>>4);
			c[1] = 0x80 | ((uc[1]&0xF)<<2) | (uc[0]>>6);
			c[2] = 0x80 | (uc[0]&0x3f);
			len = 3;
		} else if (uc[1] | (uc[0] & 0x80)) {
			if (*outbytesleft < 2) {
				LOG(log_debug, logtype_default, "short utf8 write\n");
				goto toobig;
			}
			c[0] = 0xc0 | (uc[1]<<2) | (uc[0]>>6);
			c[1] = 0x80 | (uc[0]&0x3f);
			len = 2;
		} else {
			c[0] = uc[0];
		}


		(*inbytesleft)  -= 2;
		(*outbytesleft) -= len;
		(*inbuf)  += 2;
		(*outbuf) += len;
	}

	if (*inbytesleft == 1) {
		errno = EINVAL;
		return -1;
	}

	if (*inbytesleft > 1) {
		errno = E2BIG;
		return -1;
	}
	
	return 0;

toobig:
	errno = E2BIG;
	return -1;
}

/* ------------------------ */
static int
char_ucs2_to_mac_roman ( unsigned char *r, ucs2_t wc)
{
	unsigned char c = 0;
  	if (wc < 0x0080) {
		*r = wc;
		return 1;
	}
	else if (wc >= 0x00a0 && wc < 0x0100)
		c = mac_roman_page00[wc-0x00a0];
  	else if (wc >= 0x0130 && wc < 0x0198)
		c = mac_roman_page01[wc-0x0130];
	else if (wc >= 0x02c0 && wc < 0x02e0)
		c = mac_roman_page02[wc-0x02c0];
	else if (wc == 0x03c0)
		c = 0xb9;
	else if (wc >= 0x2010 && wc < 0x2048)
		c = mac_roman_page20[wc-0x2010];
	else if (wc >= 0x2120 && wc < 0x2128)
		c = mac_roman_page21[wc-0x2120];
	else if (wc >= 0x2200 && wc < 0x2268)
		c = mac_roman_page22[wc-0x2200];
	else if (wc == 0x25ca)
		c = 0xd7;
	else if (wc >= 0xfb00 && wc < 0xfb08)
		c = mac_roman_pagefb[wc-0xfb00];
	else if (wc == 0xf8ff)
		c = 0xf0;

	if (c != 0) {
		*r = c;
		return 1;
	}
  	return 0;
}

static size_t mac_push( void *cd, char **inbuf, size_t *inbytesleft,
                         char **outbuf, size_t *outbytesleft)
{
        int len = 0;
	unsigned char *tmpptr = (unsigned char *) *outbuf;

        while (*inbytesleft >= 2 && *outbytesleft >= 1) {

		ucs2_t *inptr = (ucs2_t *) *inbuf;
		if (char_ucs2_to_mac_roman ( tmpptr, *inptr)) {
			(*inbuf) += 2;
			tmpptr++;
			len++;
			(*inbytesleft)  -= 2;
			(*outbytesleft) -= 1;
		}
		else	
		{
			errno = EILSEQ;
			return (size_t) -1;	
		}
        }

        if (*inbytesleft > 0) {
                errno = E2BIG;
                return -1;
        }

        return len;
}

/* ------------------------ */
static int
char_mac_roman_to_ucs2 (ucs2_t *pwc, const unsigned char *s)
{
	unsigned char c = *s;
  	if (c < 0x80) {
    		*pwc = (ucs2_t) c;
    		return 1;
  	}
  	else {
		unsigned short wc = mac_roman_2uni[c-0x80];
		*pwc = (ucs2_t) wc;
		return 1;
  	}
	return 0;
}

static size_t mac_pull ( void *cd, char **inbuf, size_t *inbytesleft,
                         char **outbuf, size_t *outbytesleft)
{
	ucs2_t 		*temp;
	unsigned char	*inptr;
        size_t  len = 0;

        while (*inbytesleft >= 1 && *outbytesleft >= 2) {

		inptr = (unsigned char *) *inbuf;
		temp  = (ucs2_t*) *outbuf;	
		if (char_mac_roman_to_ucs2 ( temp, inptr)) {
			(*inbuf)        +=1;
			(*outbuf)       +=2;
			(*inbytesleft) -=1;
			(*outbytesleft)-=2;
			len++;
			
		}
		else	
		{
			errno = EILSEQ;
			return (size_t) -1;	
		}
        }

        if (*inbytesleft > 0) {
                errno = E2BIG;
                return (size_t) -1;
        }

        return len;

}

/* ------------------------ 
 * from unicode to mac hebrew code page
*/
static int
char_ucs2_to_mac_hebrew ( unsigned char *r, ucs2_t wc)
{
    unsigned char c = 0;
    if (wc < 0x0080) {
       *r = wc;
       return 1;
    }
    else if (wc >= 0x00a0 && wc < 0x0100)
        c = mac_hebrew_page00[wc-0x00a0];
    else if (wc >= 0x05b0 && wc < 0x05f0)
        c = mac_hebrew_page05[wc-0x05b0];
    else if (wc >= 0x2010 && wc < 0x2028)
        c = mac_hebrew_page20[wc-0x2010];
    else if (wc == 0x20aa)
        c = 0xa6;
    else if (wc >= 0xfb18 && wc < 0xfb50)
        c = mac_hebrew_pagefb[wc-0xfb18];
    if (c != 0) {
       *r = c;
       return 1;
    }
    return 0;
}

static size_t mac_hebrew_push( void *cd, char **inbuf, size_t *inbytesleft,
                         char **outbuf, size_t *outbytesleft)
{
    unsigned char c = 0;
    int len = 0;
    unsigned char *tmpptr = (unsigned char *) *outbuf;

    while (*inbytesleft >= 2 && *outbytesleft >= 1) {
        ucs2_t *inptr = (ucs2_t *) *inbuf;
	if (*inptr == 0x05b8) {
	    (*inbuf) += 2;
	    (*inbytesleft)  -= 2;
	    if (*inbytesleft >= 2 && *((ucs2_t *)*inbuf) == 0xf87f ) {
	        (*inbuf) += 2;
	        (*inbytesleft)  -= 2;
	        c = 0xde;
	    }
	    else {
	        c = 0xcb;
	    }
	    *tmpptr = c; 
	}
	else if (*inptr == 0x05f2 && *inbytesleft >= 4 && *(inptr +1) == 0x05b7) {
	    (*inbuf) += 4;
	    (*inbytesleft)  -= 4;
	    *tmpptr = 0x81;
	}
	else if (*inptr == 0xf86a && *inbytesleft >= 6 && *(inptr +1) == 0x05dc && *(inptr +2) == 0x05b9) {
	    (*inbuf) += 6;
	    (*inbytesleft)  -= 6;
	    *tmpptr = 0xc0;
	}
	else if (char_ucs2_to_mac_hebrew ( tmpptr, *inptr)) {
	    (*inbuf) += 2;
	    (*inbytesleft)  -= 2;
	}
	else {
	    errno = EILSEQ;
	    return (size_t) -1;
	}
	(*outbytesleft) -= 1;
	tmpptr++;
	len++;
    }

    if (*inbytesleft > 0) {
        errno = E2BIG;
        return -1;
    }

    return len;
}

/* ------------------------ */
static int
char_mac_hebrew_to_ucs2 (ucs2_t *pwc, const unsigned char *s)
{
	unsigned char c = *s;
  	if (c < 0x80) {
    		*pwc = (ucs2_t) c;
    		return 1;
  	}
  	else {
		unsigned short wc = mac_hebrew_2uni[c-0x80];
		if (wc != 0xfffd) {
		    *pwc = (ucs2_t) wc;
		    return 1;
		}
  	}
	return 0;
}

static size_t mac_hebrew_pull ( void *cd, char **inbuf, size_t *inbytesleft,
                         char **outbuf, size_t *outbytesleft)
{
    ucs2_t         *temp;
    unsigned char  *inptr;
    size_t         len = 0;

    while (*inbytesleft >= 1 && *outbytesleft >= 2) {
        inptr = (unsigned char *) *inbuf;
	temp  = (ucs2_t*) *outbuf;	
	if (char_mac_hebrew_to_ucs2 ( temp, inptr)) {
	    if (*temp == 1) {       /* 0x81 --> 0x05f2+0x05b7 */
	        if (*outbytesleft < 4) {
	            errno = EILSEQ;
	            return (size_t) -1;	
	        }
	        *temp = 0x05f2;
	        *(temp +1) = 0x05b7;
	        (*outbuf)      +=4;
	        (*outbytesleft)-=4;
	        len += 2;
	    }
	    else if (*temp == 2) { /* 0xc0 -> 0xf86a 0x05dc 0x05b9*/
	        if (*outbytesleft < 6) {
	            errno = EILSEQ;
	            return (size_t) -1;	
	        }
	        *temp = 0xf86a;
	        *(temp +1) = 0x05dc;
	        *(temp +2) = 0x05b9;
	        (*outbuf)      +=6;
	        (*outbytesleft)-=6;
	        len += 3;
	    }
	    else if (*temp == 3) { /* 0xde --> 0x05b8 0xf87f */
	        if (*outbytesleft < 4) {
	            errno = EILSEQ;
	            return (size_t) -1;	
	        }
	        *temp = 0x05b8;
	        *(temp +1) = 0xf87f;
	        (*outbuf)      +=4;
	        (*outbytesleft)-=4;
	        len += 2;
	    }
	    else {
	        (*outbuf)      +=2;
		(*outbytesleft)-=2;
		len++;
	    }
	    (*inbuf)        +=1;
	    (*inbytesleft) -=1;
	}
	else	
	{
	    errno = EILSEQ;
	    return (size_t) -1;	
	}
    }

    if (*inbytesleft > 0) {
        errno = E2BIG;
        return (size_t) -1;
    }
    return len;
}

