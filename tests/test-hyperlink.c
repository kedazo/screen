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

#include <stdbool.h>
#include <string.h>

#include "../hyperlink.h"
#include "signature.h"
#include "macros.h"

SIGNATURE_CHECK(hl_parse, int, (char *, char **, char **));
SIGNATURE_CHECK(hl_intern, uint32_t, (const char *, const char *));
SIGNATURE_CHECK(hl_uri, const char *, (uint32_t));
SIGNATURE_CHECK(hl_count, size_t, (void));
SIGNATURE_CHECK(hl_gc_wanted, bool, (void));
SIGNATURE_CHECK(hl_gc_begin, void, (void));
SIGNATURE_CHECK(hl_mark, void, (uint32_t));
SIGNATURE_CHECK(hl_gc_end, void, (void));
SIGNATURE_CHECK(hl_reset, void, (void));

extern bool _mallocmock_fail;

static int parse(const char *in, char **app_id, char **uri)
{
	static char buf[HL_MAX_URI + 1024];

	strcpy(buf, in);
	return hl_parse(buf, app_id, uri);
}

static void test_parse(void)
{
	static char big[HL_MAX_URI + 16];
	char *id, *uri;

	/* plain link, no params */
	ASSERT(parse(";https://example.com/", &id, &uri) == 0);
	ASSERT(id == NULL);
	ASSERT(STREQ(uri, "https://example.com/"));

	/* close */
	ASSERT(parse(";", &id, &uri) == 0);
	ASSERT(id == NULL);
	ASSERT(STREQ(uri, ""));

	/* id param; ';' inside the URI belongs to the URI */
	ASSERT(parse("id=foo;http://a/b;c", &id, &uri) == 0);
	ASSERT(STREQ(id, "foo"));
	ASSERT(STREQ(uri, "http://a/b;c"));

	/* other params are ignored */
	ASSERT(parse("x=1:id=bar:y=2;file:///tmp", &id, &uri) == 0);
	ASSERT(STREQ(id, "bar"));
	ASSERT(STREQ(uri, "file:///tmp"));

	/* malformed: no separator */
	ASSERT(parse("https://example.com/", &id, &uri) == -1);
	ASSERT(hl_parse(NULL, &id, &uri) == -1);

	/* UTF-8 is fine */
	ASSERT(parse(";https://example.com/\xc3\xa4\xe2\x82\xac\xf0\x9f\x98\x80", &id, &uri) == 0);

	/* control characters could inject sequences into the outer terminal */
	ASSERT(parse(";http://x/\033[31m", &id, &uri) == -1);
	ASSERT(parse(";http://x/\x7f", &id, &uri) == -1);
	ASSERT(parse(";http://x/\xc2\x9c", &id, &uri) == -1);	/* C1 ST as UTF-8 */
	ASSERT(parse(";http://x/\xc2\x80", &id, &uri) == -1);
	/* invalid / overlong / surrogate UTF-8 */
	ASSERT(parse(";http://x/\xe4", &id, &uri) == -1);
	ASSERT(parse(";http://x/\xc0\xaf", &id, &uri) == -1);
	ASSERT(parse(";http://x/\xe0\x80\xaf", &id, &uri) == -1);
	ASSERT(parse(";http://x/\xed\xa0\x80", &id, &uri) == -1);
	ASSERT(parse(";http://x/\xf4\x90\x80\x80", &id, &uri) == -1);

	/* bad ids are dropped, the link itself stays valid */
	ASSERT(parse("id=\xc3\xa4;http://x/", &id, &uri) == 0);
	ASSERT(id == NULL);
	ASSERT(parse("id=;http://x/", &id, &uri) == 0);
	ASSERT(id == NULL);
	memcpy(big, "id=", 3);
	memset(big + 3, 'i', HL_MAX_ID + 1);
	strcpy(big + 3 + HL_MAX_ID + 1, ";http://x/");
	ASSERT(parse(big, &id, &uri) == 0);
	ASSERT(id == NULL);

	/* URI length limit */
	big[0] = ';';
	memset(big + 1, 'u', HL_MAX_URI);
	big[HL_MAX_URI + 1] = '\0';
	ASSERT(parse(big, &id, &uri) == 0);
	ASSERT(strlen(uri) == HL_MAX_URI);
	big[HL_MAX_URI + 1] = 'u';
	big[HL_MAX_URI + 2] = '\0';
	ASSERT(parse(big, &id, &uri) == -1);
}

static void test_intern(void)
{
	uint32_t a, b, c, d, e;

	hl_reset();
	ASSERT(hl_count() == 0);
	ASSERT(hl_uri(0) == NULL);

	/* without app id every open is a new link */
	a = hl_intern(NULL, "https://a/");
	b = hl_intern(NULL, "https://a/");
	ASSERT(a != 0 && b != 0 && a != b);
	ASSERT(b > a);
	ASSERT(STREQ(hl_uri(a), "https://a/"));
	ASSERT(STREQ(hl_uri(b), "https://a/"));

	/* with app id: same (id, uri) is the same link */
	c = hl_intern("x", "https://a/");
	d = hl_intern("x", "https://a/");
	e = hl_intern("x", "https://b/");
	ASSERT(c != 0 && c == d);
	ASSERT(e != 0 && e != c);
	ASSERT(hl_intern("y", "https://a/") != c);
	ASSERT(hl_count() == 5);

	/* rejected input */
	ASSERT(hl_intern(NULL, "") == 0);
	ASSERT(hl_intern(NULL, NULL) == 0);
	ASSERT(hl_intern(NULL, "http://x/\033") == 0);
	ASSERT(hl_count() == 5);

	/* unknown ids */
	ASSERT(hl_uri(0xfffffff0u) == NULL);

	hl_reset();
	ASSERT(hl_count() == 0);
	ASSERT(hl_uri(a) == NULL);
	/* numbering continues after a reset: ids are never reused */
	ASSERT(hl_intern(NULL, "https://c/") > e);
	hl_reset();
}

#define MANY 5000

static void test_gc(void)
{
	static uint32_t ids[MANY];
	uint32_t keyed, maxid = 0;
	size_t i;

	hl_reset();
	ASSERT(!hl_gc_wanted());
	for (i = 0; i < MANY; i++) {
		char uri[64];

		sprintf(uri, "https://example.com/%zu", i);
		ids[i] = hl_intern(NULL, uri);
		ASSERT(ids[i] != 0);
		if (ids[i] > maxid)
			maxid = ids[i];
	}
	keyed = hl_intern("keep", "https://keyed/");
	ASSERT(hl_count() == MANY + 1);
	ASSERT(hl_gc_wanted());
	for (i = 0; i < MANY; i++) {
		char uri[64];

		sprintf(uri, "https://example.com/%zu", i);
		ASSERT(STREQ(hl_uri(ids[i]), uri));
	}

	/* keep every third link and the keyed one */
	hl_gc_begin();
	for (i = 0; i < MANY; i += 3)
		hl_mark(ids[i]);
	hl_mark(keyed);
	hl_mark(0);			/* harmless */
	hl_mark(0xfffffff0u);		/* unknown: harmless */
	hl_gc_end();

	ASSERT(hl_count() == (MANY + 2) / 3 + 1);
	for (i = 0; i < MANY; i++) {
		if (i % 3 == 0)
			ASSERT(hl_uri(ids[i]) != NULL);
		else
			ASSERT(hl_uri(ids[i]) == NULL);
	}
	ASSERT(!hl_gc_wanted());

	/* dedupe still works for surviving keyed links */
	ASSERT(hl_intern("keep", "https://keyed/") == keyed);
	/* collected ids are never handed out again */
	ASSERT(hl_intern(NULL, "https://new/") > maxid);

	/* collect everything */
	hl_gc_begin();
	hl_gc_end();
	ASSERT(hl_count() == 0);
	ASSERT(hl_uri(keyed) == NULL);
	/* a collected keyed link gets a new id when it shows up again */
	ASSERT(hl_intern("keep", "https://keyed/") != keyed);
	hl_reset();
}

static void test_nomem(void)
{
	uint32_t id;
	void *probe;

	/* the mock is bypassed e.g. under valgrind: nothing to test then */
	_mallocmock_fail = true;
	probe = malloc(1);
	_mallocmock_fail = false;
	if (probe) {
		free(probe);
		return;
	}

	hl_reset();
	_mallocmock_fail = true;
	id = hl_intern(NULL, "https://oom/");
	_mallocmock_fail = false;
	ASSERT(id == 0);
	ASSERT(hl_count() == 0);
	ASSERT(hl_intern(NULL, "https://oom/") != 0);
	hl_reset();
}

int main(void)
{
	test_parse();
	test_intern();
	test_gc();
	test_nomem();
	return 0;
}
