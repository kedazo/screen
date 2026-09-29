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

#include <stdlib.h>
#include <string.h>

#include "hyperlink.h"

struct hl_entry {
	uint32_t id;
	uint32_t keyhash;	/* hash of (app_id, uri), valid if app_id != NULL */
	bool marked;		/* gc mark */
	char *app_id;		/* id= given by the application, or NULL */
	char *uri;
};

/* Two open addressing (linear probing) tables over the same entries:
 * by_id holds every entry, by_key only those with an application id.
 * Entries are only ever removed by hl_gc_end()/hl_reset(), which rebuild
 * the tables from scratch, so no tombstones are needed. */
static struct hl_entry **by_id;
static size_t by_id_cap;	/* 0 or a power of two */
static size_t n_links;
static struct hl_entry **by_key;
static size_t by_key_cap;	/* 0 or a power of two */
static size_t n_keyed;

static uint32_t next_id = 1;
static size_t gc_threshold = HL_GC_MIN;

static size_t id_slot(uint32_t id, size_t cap)
{
	return (size_t)(id * 2654435761u) & (cap - 1);
}

static uint32_t key_hash(const char *app_id, const char *uri)
{
	uint32_t h = 2166136261u;	/* FNV-1a */
	const unsigned char *p;

	for (p = (const unsigned char *)app_id; *p; p++)
		h = (h ^ *p) * 16777619u;
	h *= 16777619u;			/* the '\0' separator */
	for (p = (const unsigned char *)uri; *p; p++)
		h = (h ^ *p) * 16777619u;
	return h;
}

static void put_id(struct hl_entry **tab, size_t cap, struct hl_entry *e)
{
	size_t i = id_slot(e->id, cap);

	while (tab[i])
		i = (i + 1) & (cap - 1);
	tab[i] = e;
}

static void put_key(struct hl_entry **tab, size_t cap, struct hl_entry *e)
{
	size_t i = e->keyhash & (cap - 1);

	while (tab[i])
		i = (i + 1) & (cap - 1);
	tab[i] = e;
}

static struct hl_entry *find_id(uint32_t id)
{
	size_t i;

	if (id == 0 || by_id_cap == 0)
		return NULL;
	for (i = id_slot(id, by_id_cap); by_id[i]; i = (i + 1) & (by_id_cap - 1))
		if (by_id[i]->id == id)
			return by_id[i];
	return NULL;
}

static struct hl_entry *find_key(const char *app_id, const char *uri, uint32_t h)
{
	size_t i;

	if (by_key_cap == 0)
		return NULL;
	for (i = h & (by_key_cap - 1); by_key[i]; i = (i + 1) & (by_key_cap - 1))
		if (by_key[i]->keyhash == h && !strcmp(by_key[i]->app_id, app_id) && !strcmp(by_key[i]->uri, uri))
			return by_key[i];
	return NULL;
}

/* smallest power of two table (min 64) keeping the load factor <= 1/2 */
static size_t cap_for(size_t n)
{
	size_t cap = 64;

	while (cap < 2 * n)
		cap *= 2;
	return cap;
}

/* make room for one more entry in by_id (and by_key if keyed) */
static bool reserve(bool keyed)
{
	struct hl_entry **tab;
	size_t cap, i;

	cap = cap_for(n_links + 1);
	if (cap > by_id_cap) {
		if ((tab = calloc(cap, sizeof(*tab))) == NULL)
			return false;
		for (i = 0; i < by_id_cap; i++)
			if (by_id[i])
				put_id(tab, cap, by_id[i]);
		free(by_id);
		by_id = tab;
		by_id_cap = cap;
	}
	if (!keyed)
		return true;
	cap = cap_for(n_keyed + 1);
	if (cap > by_key_cap) {
		if ((tab = calloc(cap, sizeof(*tab))) == NULL)
			return false;
		for (i = 0; i < by_key_cap; i++)
			if (by_key[i])
				put_key(tab, cap, by_key[i]);
		free(by_key);
		by_key = tab;
		by_key_cap = cap;
	}
	return true;
}

static char *dupstr(const char *s)
{
	size_t l = strlen(s) + 1;
	char *d = malloc(l);

	if (d)
		memcpy(d, s, l);
	return d;
}

static void free_entry(struct hl_entry *e)
{
	free(e->app_id);
	free(e->uri);
	free(e);
}

/*
 * Only printable ASCII and well formed UTF-8 for code points >= U+00A0 is
 * accepted. The URI is sent to the outer terminal again later, so control
 * characters (C0, DEL and C1 - also in their UTF-8 encoding) must never
 * get through: they could terminate the OSC early and inject sequences.
 */
static bool valid_text(const char *s, bool ascii_only)
{
	const unsigned char *p = (const unsigned char *)s;

	while (*p) {
		uint32_t c = *p, cp;
		int n, i;

		if (c >= 0x20 && c < 0x7f) {
			p++;
			continue;
		}
		if (ascii_only)
			return false;
		if (c >= 0xc2 && c <= 0xdf)
			n = 1, cp = c & 0x1f;
		else if (c >= 0xe0 && c <= 0xef)
			n = 2, cp = c & 0x0f;
		else if (c >= 0xf0 && c <= 0xf4)
			n = 3, cp = c & 0x07;
		else
			return false;
		for (i = 1; i <= n; i++) {
			if ((p[i] & 0xc0) != 0x80)
				return false;
			cp = cp << 6 | (p[i] & 0x3f);
		}
		if (cp < 0xa0 || (n == 2 && cp < 0x800) || (n == 3 && cp < 0x10000)
		    || (cp >= 0xd800 && cp <= 0xdfff) || cp > 0x10ffff)
			return false;
		p += n + 1;
	}
	return true;
}

static bool valid_app_id(const char *app_id)
{
	return app_id && *app_id && strlen(app_id) <= HL_MAX_ID && valid_text(app_id, true);
}

int hl_parse(char *payload, char **app_id, char **uri)
{
	char *sep, *p, *next;

	*app_id = NULL;
	*uri = NULL;
	if (payload == NULL || (sep = strchr(payload, ';')) == NULL)
		return -1;
	*sep = '\0';
	if (strlen(sep + 1) > HL_MAX_URI || !valid_text(sep + 1, false))
		return -1;
	*uri = sep + 1;
	/* params are "key=value" pairs separated by ':', we only care for id */
	for (p = payload; p; p = next) {
		if ((next = strchr(p, ':')) != NULL)
			*next++ = '\0';
		if (!strncmp(p, "id=", 3))
			*app_id = valid_app_id(p + 3) ? p + 3 : NULL;
	}
	return 0;
}

static uint32_t new_id(void)
{
	uint32_t id;

	do {
		id = next_id++;
		if (next_id == 0)
			next_id = 1;
	} while (id == 0 || find_id(id));
	return id;
}

uint32_t hl_intern(const char *app_id, const char *uri)
{
	struct hl_entry *e;
	uint32_t h = 0;

	if (uri == NULL || *uri == '\0' || strlen(uri) > HL_MAX_URI || !valid_text(uri, false))
		return 0;
	if (app_id && !valid_app_id(app_id))
		app_id = NULL;
	if (app_id) {
		h = key_hash(app_id, uri);
		if ((e = find_key(app_id, uri, h)) != NULL)
			return e->id;
	}
	if (!reserve(app_id != NULL))
		return 0;
	if ((e = calloc(1, sizeof(*e))) == NULL)
		return 0;
	e->uri = dupstr(uri);
	e->app_id = app_id ? dupstr(app_id) : NULL;
	if (e->uri == NULL || (app_id && e->app_id == NULL)) {
		free_entry(e);
		return 0;
	}
	e->keyhash = h;
	e->id = new_id();
	put_id(by_id, by_id_cap, e);
	n_links++;
	if (app_id) {
		put_key(by_key, by_key_cap, e);
		n_keyed++;
	}
	return e->id;
}

const char *hl_uri(uint32_t id)
{
	struct hl_entry *e = find_id(id);

	return e ? e->uri : NULL;
}

size_t hl_count(void)
{
	return n_links;
}

bool hl_gc_wanted(void)
{
	return n_links >= gc_threshold;
}

void hl_gc_begin(void)
{
	size_t i;

	for (i = 0; i < by_id_cap; i++)
		if (by_id[i])
			by_id[i]->marked = false;
}

void hl_mark(uint32_t id)
{
	struct hl_entry *e = find_id(id);

	if (e)
		e->marked = true;
}

void hl_gc_end(void)
{
	struct hl_entry **nid, **nkey = NULL;
	size_t live = 0, keyed = 0, idcap, keycap = 0, i;

	for (i = 0; i < by_id_cap; i++)
		if (by_id[i] && by_id[i]->marked) {
			live++;
			if (by_id[i]->app_id)
				keyed++;
		}
	idcap = cap_for(live);
	nid = calloc(idcap, sizeof(*nid));
	if (keyed) {
		keycap = cap_for(keyed);
		nkey = calloc(keycap, sizeof(*nkey));
	}
	if (nid == NULL || (keyed && nkey == NULL)) {
		/* collection is only an optimisation: keep everything, retry later */
		free(nid);
		free(nkey);
		gc_threshold = 2 * n_links > HL_GC_MIN ? 2 * n_links : HL_GC_MIN;
		return;
	}
	for (i = 0; i < by_id_cap; i++) {
		struct hl_entry *e = by_id[i];

		if (e == NULL)
			continue;
		if (!e->marked) {
			free_entry(e);
			continue;
		}
		put_id(nid, idcap, e);
		if (e->app_id)
			put_key(nkey, keycap, e);
	}
	free(by_id);
	free(by_key);
	by_id = nid;
	by_id_cap = idcap;
	n_links = live;
	by_key = nkey;
	by_key_cap = keycap;
	n_keyed = keyed;
	gc_threshold = 2 * live > HL_GC_MIN ? 2 * live : HL_GC_MIN;
}

void hl_reset(void)
{
	size_t i;

	for (i = 0; i < by_id_cap; i++)
		if (by_id[i])
			free_entry(by_id[i]);
	free(by_id);
	free(by_key);
	by_id = by_key = NULL;
	by_id_cap = by_key_cap = 0;
	n_links = n_keyed = 0;
	gc_threshold = HL_GC_MIN;
}
