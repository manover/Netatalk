/* 
   Unix SMB/CIFS implementation.
   Character set conversion Extensions
   Copyright (C) Igor Vergeichik <iverg@mail.ru> 2001
   Copyright (C) Andrew Tridgell 2001
   Copyright (C) Simo Sorce 2001
   Copyright (C) Martin Pool 2003
   
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


/**
 * @file
 *
 * @brief Character-set conversion routines built on our iconv.
 * 
 * @note Samba's internal character set (at least in the 3.0 series)
 * is always the same as the one for the Unix filesystem.  It is
 * <b>not</b> necessarily UTF-8 and may be different on machines that
 * need i18n filenames to be compatible with Unix software.  It does
 * have to be a superset of ASCII.  All multibyte sequences must start
 * with a byte with the high bit set.
 *
 * @sa lib/iconv.c
 */


#define MAX_CHARSETS 10

static atalk_iconv_t conv_handles[MAX_CHARSETS][MAX_CHARSETS];

static char* charset_names[MAX_CHARSETS];

struct charset {
        const char *name;
	charset_t ch_charset_t;
        struct charset *prev, *next;
};

/**
 * Return the name of a charset to give to iconv().
 **/
static const char *charset_name(charset_t ch)
{
	const char *ret = NULL;

	if (ch == CH_UCS2) ret = "UCS-2LE";
	else if (ch == CH_UNIX) ret = "ASCII"; /*lp_unix_charset();*/
	else if (ch == CH_MAC) ret = "MAC"; /*lp_display_charset();*/
	else if (ch == CH_UTF8) ret = "UTF8";

	if (!ret)
		ret = charset_names[ch];

	if (!ret || !*ret) ret = "ASCII";
	return ret;
}

void lazy_initialize_conv(void)
{
	static int initialized = 0;

	if (!initialized) {
		initialized = 1;
		init_iconv();
	}
}

charset_t add_charset(char* name)
{
	static charset_t max_charset_t = NUM_CHARSETS-1;
	charset_t cur_charset_t = max_charset_t+1;
	int c1, c2;

	for (c1=0; c1<=max_charset_t;c1++) {
		if ( strcmp(name, charset_name(c1)) == 0)
			return (c1);
	}

	if ( cur_charset_t >= MAX_CHARSETS )  {
		LOG (log_debug, logtype_default, "Adding charset %s failed, too many charsets (max. %u allowed)", 
			name, MAX_CHARSETS);
		return 0;
	}

	/* First try to setup the required conversions */

	conv_handles[cur_charset_t][CH_UCS2] = atalk_iconv_open( charset_name(CH_UCS2), name);
        if (conv_handles[cur_charset_t][CH_UCS2] == (atalk_iconv_t)-1) {
		LOG(log_error, logtype_default, "Required conversion from %s to %s not supported\n",
			name,  charset_name(CH_UCS2));
		conv_handles[cur_charset_t][CH_UCS2] = NULL;
		return 0;
	}

	conv_handles[CH_UCS2][cur_charset_t] = atalk_iconv_open( name, charset_name(CH_UCS2));
        if (conv_handles[CH_UCS2][cur_charset_t] == (atalk_iconv_t)-1) {
		LOG(log_error, logtype_default, "Required conversion from %s to %s not supported\n",
			charset_name(CH_UCS2), name);
		conv_handles[CH_UCS2][cur_charset_t] = NULL;
		return 0;
	}

	/* register the new charset_t name */
	charset_names[cur_charset_t] = strdup(name);

	
	for (c1=0;c1<=cur_charset_t;c1++) {
		for (c2=0;c2<=cur_charset_t;c2++) {
			const char *n1 = charset_name((charset_t)c1);
			const char *n2 = charset_name((charset_t)c2);
			if (conv_handles[c1][c2] &&
			    strcmp(n1, conv_handles[c1][c2]->from_name) == 0 &&
			    strcmp(n2, conv_handles[c1][c2]->to_name) == 0)
				continue;

			if (conv_handles[c1][c2])
				atalk_iconv_close(conv_handles[c1][c2]);

			conv_handles[c1][c2] = atalk_iconv_open(n2,n1);
			if (conv_handles[c1][c2] == (atalk_iconv_t)-1) {
				LOG(log_debug, logtype_default, "Conversion from %s to %s not supported\n",
					 charset_name((charset_t)c1), charset_name((charset_t)c2));
				conv_handles[c1][c2] = NULL;
			}
		}
	}

	max_charset_t++;

	LOG(log_debug, logtype_default, "Added charset %s with handle %u", name, cur_charset_t);
	return (cur_charset_t);
}

/**
 * Initialize iconv conversion descriptors.
 *
 * This is called the first time it is needed, and also called again
 * every time the configuration is reloaded, because the charset or
 * codepage might have changed.
 **/
void init_iconv(void)
{
	int c1, c2;

	/* so that charset_name() works we need to get the UNIX<->UCS2 going
	   first */
	if (!conv_handles[CH_UNIX][CH_UCS2])
		conv_handles[CH_UNIX][CH_UCS2] = atalk_iconv_open("UCS-2LE", "ASCII");

	if (!conv_handles[CH_UCS2][CH_UNIX])
		conv_handles[CH_UCS2][CH_UNIX] = atalk_iconv_open("ASCII", "UCS-2LE");

	for (c1=0;c1<NUM_CHARSETS;c1++) {
		for (c2=0;c2<NUM_CHARSETS;c2++) {
			const char *n1 = charset_name((charset_t)c1);
			const char *n2 = charset_name((charset_t)c2);
			if (conv_handles[c1][c2] &&
			    strcmp(n1, conv_handles[c1][c2]->from_name) == 0 &&
			    strcmp(n2, conv_handles[c1][c2]->to_name) == 0)
				continue;

			if (conv_handles[c1][c2])
				atalk_iconv_close(conv_handles[c1][c2]);

			conv_handles[c1][c2] = atalk_iconv_open(n2,n1);
			if (conv_handles[c1][c2] == (atalk_iconv_t)-1) {
				LOG(log_debug, logtype_default, "Conversion from %s to %s not supported\n",
					 charset_name((charset_t)c1), charset_name((charset_t)c2));
				conv_handles[c1][c2] = NULL;
			}
		}
	}
}

/**
 * Convert string from one encoding to another, making error checking etc
 *
 * @param src pointer to source string (multibyte or singlebyte)
 * @param srclen length of the source string in bytes
 * @param dest pointer to destination string (multibyte or singlebyte)
 * @param destlen maximal length allowed for string
 * @returns the number of bytes occupied in the destination
 **/
size_t convert_string(charset_t from, charset_t to,
		      void const *src, size_t srclen, 
		      void *dest, size_t destlen)
{
	size_t i_len, o_len;
	size_t retval;
	const char* inbuf = (const char*)src;
	char* outbuf = (char*)dest;
	atalk_iconv_t descriptor;

	if (srclen == (size_t)-1)
		srclen = strlen(src)+1;

	lazy_initialize_conv();

	descriptor = conv_handles[from][to];

	if (descriptor == (atalk_iconv_t)-1 || descriptor == (atalk_iconv_t)0) {
		/* conversion not supported, use as is */
		size_t len = MIN(srclen,destlen);
		memcpy(dest,src,len);
		return len;
	}

	i_len=srclen;
	o_len=destlen;
	retval = atalk_iconv(descriptor,  &inbuf, &i_len, &outbuf, &o_len);
	if(retval==(size_t)-1) {
	    	const char *reason="unknown error";
		switch(errno) {
			case EINVAL:
				reason="Incomplete multibyte sequence";
				break;
			case E2BIG:
				reason="No more room"; 
				LOG(log_debug, logtype_default, "convert_string: Required %d, available %d\n",
					srclen, destlen);
				/* we are not sure we need srclen bytes,
			          may be more, may be less.
				  We only know we need more than destlen
				  bytes ---simo */
		               break;
			case EILSEQ:
			       reason="Illegal multibyte sequence";
			       break;
		}
		return (size_t)-1;
		/* smb_panic(reason); */
	}
	return destlen-o_len;
}

/**
 * Convert between character sets, allocating a new buffer for the result.
 *
 * @param srclen length of source buffer.
 * @param dest always set at least to NULL
 * @note -1 is not accepted for srclen.
 *
 * @returns Size in bytes of the converted string; or -1 in case of error.
 **/

size_t convert_string_allocate(charset_t from, charset_t to,
			       void const *src, size_t srclen, void **dest)
{
	size_t i_len, o_len, destlen;
	size_t retval;
	const char *inbuf = (const char *)src;
	char *outbuf, *ob;
	atalk_iconv_t descriptor;

	*dest = NULL;

	if (src == NULL || srclen == (size_t)-1)
		return (size_t)-1;

	lazy_initialize_conv();

	descriptor = conv_handles[from][to];

	if (descriptor == (atalk_iconv_t)-1 || descriptor == (atalk_iconv_t)0) {
		/* conversion not supported, return -1*/
		LOG(log_debug, logtype_default, "convert_string_allocate: conversion not supported!\n");
		return -1;
	}

	destlen = MAX(srclen, 512);
	outbuf = NULL;
convert:
	destlen = destlen * 2;
	ob = (char *)realloc(outbuf, destlen);
	if (!ob) {
		LOG(log_debug, logtype_default,"convert_string_allocate: realloc failed!\n");
		SAFE_FREE(outbuf);
		return (size_t)-1;
	} else {
		outbuf = ob;
	}
	i_len = srclen;
	o_len = destlen;
	retval = atalk_iconv(descriptor,
			   &inbuf, &i_len,
			   &outbuf, &o_len);
	if(retval == (size_t)-1) 		{
	    	const char *reason="unknown error";
		switch(errno) {
			case EINVAL:
				reason="Incomplete multibyte sequence";
				break;
			case E2BIG:
				goto convert;		
			case EILSEQ:
				reason="Illegal multibyte sequence";
				break;
		}
		LOG(log_debug, logtype_default,"Conversion error: %s(%s)\n",reason,inbuf);
		/* smb_panic(reason); */
		return (size_t)-1;
	}
	
	destlen = destlen - o_len;
	*dest = (char *)realloc(ob,destlen);
	if (destlen && !*dest) {
		LOG(log_debug, logtype_default, "convert_string_allocate: out of memory!\n");
		SAFE_FREE(ob);
		return (size_t)-1;
	}

	return destlen;
}


size_t unix_strupper(const char *src, size_t srclen, char *dest, size_t destlen)
{
	size_t size;
	ucs2_t *buffer;
	
	size = convert_string_allocate(CH_UNIX, CH_UCS2, src, srclen,
				       (void **) &buffer);
	if (size == -1) {
		free(buffer);
		return size;
	}
	if (!strupper_w(buffer) && (dest == src)) {
		free(buffer);
		return srclen;
	}
	
	size = convert_string(CH_UCS2, CH_UNIX, buffer, size, dest, destlen);
	free(buffer);
	return size;
}

size_t unix_strlower(const char *src, size_t srclen, char *dest, size_t destlen)
{
	size_t size;
	ucs2_t *buffer;
	
	size = convert_string_allocate(CH_UNIX, CH_UCS2, src, srclen,
				       (void **) &buffer);
	if (size == -1) {
		free(buffer);
		return size;
	/*	smb_panic("failed to create UCS2 buffer");*/
	}
	if (!strlower_w(buffer) && (dest == src)) {
		free(buffer);
		return srclen;
	}
	size = convert_string(CH_UCS2, CH_UNIX, buffer, size, dest, destlen);
	free(buffer);
	return size;
}

size_t utf8_strupper(const char *src, size_t srclen, char *dest, size_t destlen)
{
	size_t size;
	ucs2_t *buffer;
	
	size = convert_string_allocate(CH_UTF8, CH_UCS2, src, srclen,
				       (void **) &buffer);
	if (size == -1) {
		free(buffer);
		return size;
	}
	if (!strupper_w(buffer) && (dest == src)) {
		free(buffer);
		return srclen;
	}
	
	size = convert_string(CH_UCS2, CH_UTF8, buffer, size, dest, destlen);
	free(buffer);
	return size;
}

size_t utf8_strlower(const char *src, size_t srclen, char *dest, size_t destlen)
{
	size_t size;
	ucs2_t *buffer;
	
	size = convert_string_allocate(CH_UTF8, CH_UCS2, src, srclen,
				       (void **) &buffer);
	if (size == -1) {
		free(buffer);
		return size;
	}
	if (!strlower_w(buffer) && (dest == src)) {
		free(buffer);
		return srclen;
	}
	
	size = convert_string(CH_UCS2, CH_UTF8, buffer, size, dest, destlen);
	free(buffer);
	return size;
}

size_t mac_strupper(const char *src, size_t srclen, char *dest, size_t destlen)
{
	size_t size;
	ucs2_t *buffer;
	
	size = convert_string_allocate(CH_MAC, CH_UCS2, src, srclen,
				       (void **) &buffer);
	if (size == -1) {
		free(buffer);
		return size;
	}
	if (!strupper_w(buffer) && (dest == src)) {
		free(buffer);
		return srclen;
	}
	
	size = convert_string(CH_UCS2, CH_MAC, buffer, size, dest, destlen);
	free(buffer);
	return size;
}

size_t mac_strlower(const char *src, size_t srclen, char *dest, size_t destlen)
{
	size_t size;
	ucs2_t *buffer;
	
	size = convert_string_allocate(CH_MAC, CH_UCS2, src, srclen,
				       (void **) &buffer);
	if (size == -1) {
		free(buffer);
		return size;
	}
	if (!strlower_w(buffer) && (dest == src)) {
		free(buffer);
		return srclen;
	}
	
	size = convert_string(CH_UCS2, CH_MAC, buffer, size, dest, destlen);
	free(buffer);
	return size;
}

/**
 * Copy a string from a mac char* src to a UCS2 destination, allocating a buffer
 *
 * @param dest always set at least to NULL 
 *
 * @returns The number of bytes occupied by the string in the destination
 *         or -1 in case of error.
 **/

size_t mac_to_ucs2_allocate(ucs2_t **dest, const char *src)
{
	size_t src_len = strlen(src)+1;

	*dest = NULL;
	return convert_string_allocate(CH_MAC, CH_UCS2, src, src_len, (void **)dest);	
}

/**
 * Copy a string from a unix char* src to a UTF-8 destination, allocating a buffer
 *
 * @param dest always set at least to NULL 
 *
 * @returns The number of bytes occupied by the string in the destination
 **/

size_t mac_to_utf8_allocate(char **dest, const char *src)
{
	size_t src_len = strlen(src)+1;

	*dest = NULL;
	return convert_string_allocate(CH_MAC, CH_UTF8, src, src_len, (void **)dest);	
}

/**
 * Copy a string from a UCS2 src to a unix char * destination, allocating a buffer
 *
 * @param dest always set at least to NULL 
 *
 * @returns The number of bytes occupied by the string in the destination
 **/

size_t ucs2_to_mac_allocate(char **dest, const ucs2_t *src)
{
	size_t src_len = (strlen_w(src)+1) * sizeof(ucs2_t);
	*dest = NULL;
	return convert_string_allocate(CH_UCS2, CH_MAC, src, src_len, (void **)dest);	
}

/**
 * Copy a string from a UTF-8 src to a unix char * destination, allocating a buffer
 *
 * @param dest always set at least to NULL 
 *
 * @returns The number of bytes occupied by the string in the destination
 **/

static char convbuf[MAXPATHLEN+1];
size_t utf8_to_mac_allocate(void **dest, const char *src)
{
	size_t src_len = strlen(src)+1;
	*dest = NULL;

	src_len = utf8_precompose ( (char *) src, src_len, convbuf, MAXPATHLEN);	
	return convert_string_allocate(CH_UTF8, CH_MAC, convbuf, src_len, dest);	
}

size_t utf8_to_mac ( char* src, size_t src_len, char* dest, size_t dest_len)
{
	src_len = utf8_precompose ( (char *) src, src_len, convbuf, MAXPATHLEN);	
	return convert_string(CH_UTF8, CH_MAC, convbuf, src_len, dest, dest_len);	
}

static char  debugbuf[ MAXPATHLEN +1 ];
char * debug_out ( char * seq, size_t len)
{
        size_t i = 0;
        unsigned char *p;
        char *q;

        p = (unsigned char*) seq;
        q = debugbuf;

        for ( i = 0; i<=(len-1); i++)
        {
                sprintf(q, "%2.2x.", *p);
                q += 3;
                p++;
        }
        *q=0;
        q = debugbuf;
        return q;
}


size_t utf8_precompose ( char * src, size_t inlen, char * dst, size_t outlen)
{
	char *u;
	size_t len;
	size_t ilen;

        if ((size_t)(-1) == (len =  convert_string(CH_UTF8, CH_UCS2, src, inlen, convbuf, MAXPATHLEN)) )
            return len;

        if ( NULL == (u = precompose_w((ucs2_t *)convbuf, len, &ilen)) )
	    return (size_t)(-1);

        if ((size_t)(-1) == (len = convert_string( CH_UCS2, CH_UTF8, u, ilen, dst, outlen)) )
	    return (size_t)(-1);

	dst[len] = 0;
	return (len);
}

size_t utf8_decompose ( char * src, size_t inlen, char * dst, size_t outlen)
{
	char *u;
	size_t len;
	size_t ilen;

        if ((size_t)(-1) == (len =  convert_string(CH_UTF8, CH_UCS2, src, inlen, convbuf, MAXPATHLEN)) )
            return len;

        if ( NULL == (u = decompose_w((ucs2_t *)convbuf, len, &ilen)) )
	    return (size_t)(-1);

        if ((size_t)(-1) == (len = convert_string( CH_UCS2, CH_UTF8, u, ilen, dst, outlen)) )
	    return (size_t)(-1);

	dst[len] = 0;
	return (len);
}


size_t utf8_to_mac_charset ( charset_t ch, char* src, size_t src_len, char* dest, size_t dest_len, int* mangle)
{
	size_t i_len, o_len;
	size_t retval;
	const char* inbuf;
	char* outbuf = (char*)dest;
	atalk_iconv_t descriptor;

	lazy_initialize_conv();

	src_len = utf8_precompose ( (char *) src, src_len+1, convbuf, MAXPATHLEN);

	descriptor = conv_handles[CH_UTF8][ch];

	if (descriptor == (atalk_iconv_t)-1 || descriptor == (atalk_iconv_t)0) {
		LOG(log_error, logtype_default, "Conversion not supported ( UTF8 to %s )", charset_name(ch));
  		return (size_t)(-1);	
	}

	inbuf = (const char*) convbuf;
	i_len=src_len;
	o_len=dest_len;

	retval = atalk_iconv_ignore(descriptor,  &inbuf, &i_len, &outbuf, &o_len, mangle);

	if(retval==(size_t)-1) 
  		return (size_t)(-1);	
	
	dest[dest_len-o_len] = 0;
	return dest_len-o_len;
}

