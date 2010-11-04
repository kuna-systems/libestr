/**
 * @file string.c
 * Implements string handling
 *
 * @note: for efficiency reasons, later builds may spread the
 * individual functions across different source modules. I was a 
 * bit lazy to do this right now and I am totally unsure if it
 * really is worth the effort.
 *//*
 * libestr - some essentials for string handling (and a bit more)
 * Copyright 2010 by Rainer Gerhards and Adiscon GmbH.
 *
 * This file is part of libestr.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * A copy of the LGPL v2.1 can be found in the file "COPYING" in this distribution.
 */
#include "config.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

#include "libestr.h"

#define ERR_ABORT {r = 1; goto done; }

#if !defined(NDEBUG)
#	define CHECK_STR 
#	define ASSERT_STR 
#else
#	define CHECK_STR \
		if(s->objID != ES_STRING_OID) { \
			r = -1; \
			goto done; \
		}
#	define ASSERT_STR(s) assert((s)->objID == ES_STRING_OID)
#endif /* #if !defined(NDEBUG) */


/* ------------------------------ HELPERS ------------------------------ */

/**
 * Extend string buffer.
 * This is called if the size is insufficient. Note that the string
 * pointer will be changed.
 * @param[in/out] ps pointer to (pointo to) string to be extened
 * @param[in] minNeeded minimum number of additional bytes needed
 * @returns 0 on success, something else otherwise
 */
int
es_extendBuf(es_str_t **ps, size_t minNeeded)
{
	int r = 0;
	es_str_t *s = *ps;
	size_t newSize;

	ASSERT_STR(s);
	/* first compute the new size needed */
	if(minNeeded > s->allocInc) {
		/* TODO: think about optimizing this based on allocInc */
		newSize = minNeeded;
	} else {
		newSize = s->lenBuf + s->allocInc;
		/* set new allocInc for fast growing string */
		if(2 * s->allocInc > 65535) /* prevent overflow! */
			s->allocInc = 65535;
		else
			s->allocInc = 2 * s->allocInc;
	}
	newSize += s->lenBuf + sizeof(es_str_t); /* add current size */

	if((s = (es_str_t*) realloc(s, newSize)) == NULL) {
		r = errno;
		goto done;
	}
	s->lenBuf = newSize;
	*ps = s;

done:
	return r;
}


/* ------------------------------ END HELPERS ------------------------------ */

es_str_t *
es_newStr(size_t lenhint)
{
	es_str_t *s;
	/* we round length to a multiple of 8 in the hope to reduce
	 * memory fragmentation.
	 */
	if(lenhint & 0x07)
		lenhint = lenhint - (lenhint & 0x07) + 8;

	if((s = malloc(sizeof(es_str_t) + lenhint)) == NULL)
		goto done;

#	ifndef NDEBUG
	s->objID = ES_STRING_OID;
#	endif
	s->lenBuf = lenhint;

done:
	return s;
}


es_str_t*
es_newStrFromCStr(char *cstr, size_t len)
{
	es_str_t *s;
	assert(strlen(cstr) == len);
	
	if((s = es_newStr(len)) == NULL) goto done;
	memcpy(es_getBufAddr(s), cstr, len);
	s->lenStr = len;

done:
	return s;
}


void
es_deleteStr(es_str_t *s)
{
	ASSERT_STR(s);
#	if !defined(NDEBUG)
	s->objID = ES_STRING_FREED;
#	endif
	free(s);
}


int
es_strcmp(es_str_t *s1, es_str_t *s2)
{
	int r;
	size_t i;
	unsigned char *c1, *c2;

	ASSERT_STR(s1);
	ASSERT_STR(s2);
	if(s1->lenStr < s2->lenStr)
		r = -1;
	else if(s1->lenStr > s2->lenStr)
		r = 1;
	else {
		c1 = es_getBufAddr(s1);
		c2 = es_getBufAddr(s2);
		r = 0;	/* assume: strings equal, will be reset if not */
		for(i = 0 ; i < s1->lenStr ; ++i) {
			if(c1[i] != c2[i]) {
				r = c1[i] - c2[i];
				break;
			}
		}
	}
	return r;
}


int
es_addBuf(es_str_t **ps1, char *buf, size_t lenBuf)
{
	int r;
	size_t newlen;
	es_str_t *s1 = *ps1;

	ASSERT_STR(s1);

	newlen = s1->lenStr + lenBuf;
	if(s1->lenBuf < newlen) {
		/* we need to extend */
		if((r = es_extendBuf(ps1, newlen - s1->lenBuf)) != 0) goto done;
		s1 = *ps1;
	}
	
	/* do the actual copy, we now *have* the space required */
	memcpy(es_getBufAddr(s1)+s1->lenStr, buf, lenBuf);
	s1->lenStr = newlen;
	r = 0; /* all well */

done:
	return r;
}


char *
es_str2cstr(es_str_t *s, char *nulEsc)
{
	char *cstr;
	size_t lenEsc;
	int nbrNUL;
	size_t i, iDst;
	unsigned char *c;

	/* detect number of NULs inside string */
	c = es_getBufAddr(s);
	nbrNUL = 0;
	for(i = 0 ; i < s->lenStr ; ++i) {
		if(c[i] == 0x00)
			++nbrNUL;
	}

	if(nbrNUL == 0) {
		/* no special handling needed */
		if((cstr = malloc(s->lenStr + 1)) == NULL) goto done;
		memcpy(cstr, c, s->lenStr);
		cstr[s->lenStr] = '\0';
	} else {
		/* we have NUL bytes present and need to process them
		 * during creation of the C string.
		 */
		lenEsc = (nulEsc == NULL) ? 0 : strlen(nulEsc);
		if((cstr = malloc(s->lenStr + nbrNUL * (lenEsc - 1) + 1)) == NULL) goto done;
		for(i = iDst = 0 ; i < s->lenStr ; ++i) {
			if(c[i] == 0x00) {
				if(lenEsc == 1) {
					cstr[iDst++] = *nulEsc;
				} else {
					memcpy(cstr + iDst, nulEsc, lenEsc);
					iDst += lenEsc;
				}
			} else {
				cstr[iDst++] = c[i];
			}
		}
		cstr[iDst] = '\0';
	}

done:
	return cstr;
}