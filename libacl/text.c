/*
  Original version;
  Copyright (C) 1999, 2000, 2001
  Andreas Gruenbacher, <a.gruenbacher@computer.org>

  SGI modifications;
  Copyright (C) 2001 Silicon Graphics, Inc.

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU Library General Public
  License as published by the Free Software Foundation; either
  version 2 of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Library General Public License for more details.

  You should have received a copy of the GNU Library General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <acl/libacl.h>
#include <pwd.h>
#include <grp.h>


#define setoserror(x)	(errno = (x))

/*
  Print an ACL to a stream.

  returns
	the number of characters written, or -1 on error.
 */
int
acl_print (FILE *file, acl_t acl, const char *prefix, int options)
{
	int i, n, len, written = 0, size = 256;
	acl_entry_t mask = NULL;
	char *text;

	if (!acl) {
		setoserror (EINVAL);
		return -1;
	}
	if ((text = (char *) malloc (size)) == NULL) {
		setoserror (ENOMEM);
		return -1;
	}

	if (options & (TEXT_SOME_EFFECTIVE|TEXT_ALL_EFFECTIVE)) {
		/* fetch the ACL_MASK entry */
		for (i = 0; i < acl->acl_cnt; i++) {
			if (acl->acl_entry[i].ae_tag == ACL_MASK) {
				mask = &acl->acl_entry[i];
                                break;
                        }
		}
	}

	for (i = 0; i < acl->acl_cnt; i++) {
		len = acl_entry_to_any_str (&acl->acl_entry[i], text,
					    size, mask, prefix, options);
		if (len < 0) {
			free (text);
			return -1;
		}
		if (size < len) {
			while (size < len)
				size <<= 1;
			if ((text = (char *) realloc (text, size)) == NULL)
				return -1;
			continue;
		}
		if ((n = fprintf (file, "%s\n", text)) < 0) {
			free (text);
			return -1;
		}
		written += n;
	}
	free (text);
	return written;
}

char *
acl_to_any_text (acl_t acl, ssize_t *len_p, const char *prefix,
		 char separator, const char *suffix, int options)
{
	acl_entry_t mask = NULL;
	char *text = NULL;
	int i, size, len, entry_len, suffix_len;

	if (!acl)
		return NULL;

	len = entry_len = 0;
	size = acl->acl_cnt * 15 + 1;
	suffix_len = suffix ? strlen(suffix) : 0;
	if ((text = malloc (size)) == NULL) {
		setoserror (ENOMEM);
		return NULL;
	}

	if (options & (TEXT_SOME_EFFECTIVE|TEXT_ALL_EFFECTIVE)) {
		/* fetch the ACL_MASK entry */
		for (i = 0; i < acl->acl_cnt; i++) {
			if (acl->acl_entry[i].ae_tag == ACL_MASK) {
				mask = &acl->acl_entry[i];
                                break;
                        }
		}
	}

	for (i = 0; i < acl->acl_cnt; i++) {
		while (len + entry_len + 1 > size)
			size <<= 1;
		if ((text = realloc(text, size)) == NULL) {
			setoserror (ENOMEM);
			return NULL;
		}
		entry_len = acl_entry_to_any_str (&acl->acl_entry[i],
				text + len, size - len, mask, prefix, options);
		if (entry_len < 0) {
			free (text);
			setoserror (ENOMEM);
			return NULL;
		}
		if (len + entry_len + suffix_len + 1 > size)
			continue;
		len += entry_len;
		text[len] = separator;
		len++;
	}
	if (len)
		len--;
	if (suffix) {
		len += suffix_len;
		strcpy(text + len, suffix);
	}
	else
		text[len] = '\0';
	if (len_p)
		*len_p = len;
	return text;
}


/*
  This function is equivalent to the proposed changes to snprintf:
    snprintf(text_p, size, "%u", i)
  (The current snprintf returns -1 if the buffer is too small; the proposal
   is to return the number of characters that would be required. See the
   snprintf manual page.)
*/

static ssize_t
snprint_uint(
	char *text_p,
	ssize_t size,
	unsigned int i)
{
	unsigned int tmp = i;
	int digits = 1;
	unsigned int factor = 1;

	while ((tmp /= 10) != 0) {
		digits++;
		factor *= 10;
	}
	if (size && (i == 0)) {
		*text_p++ = '0';
	} else {
		while (size > 0 && factor > 0) {
			*text_p++ = '0' + (i / factor);
			size--;
			i %= factor;
			factor /= 10;
		}
	}
	if (size)
		*text_p = '\0';

	return digits;
}

static const char *
user_name(uid_t uid)
{
	struct passwd *passwd = getpwuid(uid);

	if (passwd != NULL)
		return passwd->pw_name;
	else
		return NULL;
}

static const char *
group_name(gid_t gid)
{
	struct group *group = getgrgid(gid);

	if (group != NULL)
		return group->gr_name;
	else
		return NULL;
}


/*
  Convert an ACL entry to text form.

  text_p
  	points to a buffer
  size
  	the size of text_p
  Returns
  	the size of the buffer that was used, or that
	would be used (if size is too small).
*/

#define EFFECTIVE_STR		"#effective:"

#define ADVANCE(x) \
	text_p += (x); \
	size -= (x); \
	if (size < 0) \
		size = 0;

#define ABBREV(s, str_len) \
	if (options & TEXT_ABBREVIATE) { \
		if (size > 0) \
			text_p[0] = *(s); \
		if (size > 1) \
			text_p[1] = ':'; \
		ADVANCE(2); \
	} else { \
		strncpy(text_p, (s), size); \
		ADVANCE(str_len); \
	}

ssize_t
acl_entry_to_any_str(
	const acl_entry_t entry_d,
	char *text_p,
	ssize_t size,
	const acl_entry_t mask_d,
	const char *prefix,
	int options)
{
	#define TABS 4
	static const char *tabs = "\t\t\t\t";
	acl_entry_t entry_obj_p = entry_d;
	acl_entry_t mask_obj_p = NULL;
	acl_perm_t effective;
	acl_tag_t type;
	ssize_t x;
	const char *orig_text_p = text_p, *str;
	if (!entry_obj_p)
		return -1;
	if (mask_d)
		mask_obj_p = mask_d;
	if (text_p == NULL)
		size = 0;

	if (prefix) {
		strncpy(text_p, prefix, size);
		ADVANCE(strlen(prefix));
	}

	type = entry_obj_p->ae_tag;
	switch (type) {
		case ACL_USER_OBJ:  /* owner */
			mask_obj_p = NULL;
			/* fall through */
		case ACL_USER:  /* additional user */
			//strncpy(text_p, "user:", size);
			//ADVANCE(5);
			ABBREV("user:", 5);
			if (type == ACL_USER) {
				if (options & TEXT_NUMERIC_IDS)
					str = NULL;
				else
					str = user_name(entry_obj_p->ae_id);
				if (str != NULL) {
					strncpy(text_p, str, size);
					ADVANCE(strlen(str));
				} else {
					x = snprint_uint(text_p, size,
					             entry_obj_p->ae_id);
					ADVANCE(x);
				}
			}
			if (size > 0)
				*text_p = ':';
			ADVANCE(1);
			break;

		case ACL_GROUP_OBJ:  /* owning group */
		case ACL_GROUP:  /* additional group */
			//strncpy(text_p, "group:", size);
			//ADVANCE(6);
			ABBREV("group:", 6);
			if (type == ACL_GROUP) {
				if (options & TEXT_NUMERIC_IDS)
					str = NULL;
				else
					str = group_name(entry_obj_p->ae_id);
				if (str != NULL) {
					strncpy(text_p, str, size);
					ADVANCE(strlen(str));
				} else {
					x = snprint_uint(text_p, size,
					             entry_obj_p->ae_id);
					ADVANCE(x);
				}
			}
			if (size > 0)
				*text_p = ':';
			ADVANCE(1);
			break;

			
		case ACL_MASK:  /* acl mask */
			//strncpy(text_p, "mask:", size);
			//ADVANCE(5);
			ABBREV("mask:", 5);
			if (size > 0)
				*text_p = ':';
			ADVANCE(1);
			break;

		case ACL_OTHER:  /* other users */
			mask_obj_p = NULL;
			/* fall through */
			//strncpy(text_p, "other:", size);
			//ADVANCE(6);
			ABBREV("other:", 6);
			if (size > 0)
				*text_p = ':';
			ADVANCE(1);
			break;

		default:
			return 0;
	}

	switch ((size >= 3) ? 3 : size) {
		case 3:
			text_p[2] = (entry_obj_p->ae_perm &
			             ACL_EXECUTE) ? 'x' : '-'; 
			/* fall through */
		case 2:
			text_p[1] = (entry_obj_p->ae_perm &
			             ACL_WRITE) ? 'w' : '-'; 
			/* fall through */
		case 1:
			text_p[0] = (entry_obj_p->ae_perm &
			             ACL_READ) ? 'r' : '-'; 
			break;
	}
	ADVANCE(3);

	if (mask_obj_p &&
	    (options & (TEXT_SOME_EFFECTIVE|TEXT_ALL_EFFECTIVE))) {
		mask_obj_p = mask_d;
		if (!mask_obj_p)
			return -1;

		effective = entry_obj_p->ae_perm &
		                 mask_obj_p->ae_perm;
		if (options & TEXT_NO_EFFECTIVE)
			effective = ~ACL_PERM_NONE;
		else if (options & TEXT_ALL_EFFECTIVE)
			effective = ACL_PERM_NONE;

		if (effective != entry_obj_p->ae_perm) {
			x = (options & TEXT_SMART_INDENT) ?
				((text_p - orig_text_p)/8) : TABS-1;
			strncpy(text_p, tabs+x, size);
			ADVANCE(TABS-x);

			strncpy(text_p, EFFECTIVE_STR, size);
			ADVANCE(sizeof(EFFECTIVE_STR)-1);

			switch ((size >= 3) ? 3 : size) {
				case 3:
					text_p[2] = (effective &
						     ACL_EXECUTE) ? 'x' : '-'; 
					/* fall through */
				case 2:
					text_p[1] = (effective &
						     ACL_WRITE) ? 'w' : '-'; 
					/* fall through */
				case 1:
					text_p[0] = (effective &
						     ACL_READ) ? 'r' : '-'; 
					break;
			}
			ADVANCE(3);

		}
	}

	/* zero-terminate string (but don't count '\0' character) */
	if (size > 0)
		*text_p = '\0';
	
	return (text_p - orig_text_p);  /* total size required, excluding
	                                   final NULL character. */
}