/* Copyright (c) 2026
 *      David Kedves (kedz@kedz.eu)
 *
 * This file is part of GNU screen.
 *
 * GNU screen is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (see the file COPYING); if not, see
 * <https://www.gnu.org/licenses>.
 *
 ****************************************************************
 */

#ifndef SCREEN_HYPERLINK_H
#define SCREEN_HYPERLINK_H

/*
 * OSC 8 hyperlink table.
 *
 * Every cell carries a 32 bit link id (struct mchar.linkid), 0 meaning
 * "no link". Ids are process wide, handed out monotonically and never
 * reused, so a stale id can only ever miss on lookup; it can never
 * resolve to somebody else's URI.
 *
 * Unreferenced entries are reclaimed by mark & sweep: when hl_gc_wanted()
 * says so, the caller runs hl_gc_begin(), hl_mark()s every id still
 * referenced anywhere and finishes with hl_gc_end().
 *
 * This module deliberately knows nothing about windows or displays so it
 * can be unit tested on its own (tests/test-hyperlink.c).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HL_MAX_URI	8192	/* longest URI we store */
#define HL_MAX_ID	250	/* longest application supplied id= we honour */
#define HL_GC_MIN	1024	/* never collect below this many live links */

/*
 * Split an OSC 8 payload ("params;URI", i.e. the part after "8;") in place.
 * *app_id is set to the id= parameter (NULL if absent or too long), *uri to
 * the URI ("" means: close the current link). Returns -1 for a malformed
 * payload (no ';' separator or URI longer than HL_MAX_URI), 0 otherwise.
 */
int hl_parse(char *payload, char **app_id, char **uri);

/*
 * Register a link and return its id. Links with the same application id
 * and URI share one id (so an application's multi part link stays one
 * link); links without an application id always get a fresh one.
 * Returns 0 if the link could not be stored (out of memory, bad input).
 */
uint32_t hl_intern(const char *app_id, const char *uri);

/* URI of a link, or NULL if the id is 0 or unknown (e.g. already collected) */
const char *hl_uri(uint32_t id);

/* number of live links */
size_t hl_count(void);

/* true once enough links accumulated that a collection is worthwhile */
bool hl_gc_wanted(void);

void hl_gc_begin(void);
void hl_mark(uint32_t id);
void hl_gc_end(void);

/* drop every link (id numbering continues) */
void hl_reset(void);

#endif /* SCREEN_HYPERLINK_H */
