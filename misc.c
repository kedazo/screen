/* Copyright (c) 2008, 2009
 *      Juergen Weigert (jnweiger@immd4.informatik.uni-erlangen.de)
 *      Michael Schroeder (mlschroe@immd4.informatik.uni-erlangen.de)
 *      Micah Cowan (micah@cowan.name)
 *      Sadrul Habib Chowdhury (sadrul@users.sourceforge.net)
 * Copyright (c) 1993-2002, 2003, 2005, 2006, 2007
 *      Juergen Weigert (jnweiger@immd4.informatik.uni-erlangen.de)
 *      Michael Schroeder (mlschroe@immd4.informatik.uni-erlangen.de)
 * Copyright (c) 1987 Oliver Laumann
 *
 * This program is free software; you can redistribute it and/or modify
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
 * https://www.gnu.org/licenses/, or contact Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02111-1301  USA
 *
 ****************************************************************
 */

#include "config.h"

#include "misc.h"

#include <poll.h>
#include <sys/types.h>
#include <sys/stat.h>		/* mkdir() declaration */
#include <signal.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <fcntl.h>

#include "screen.h"

char *SaveStr(const char *str)
{
	char *cp = strdup(str);

	if (cp == NULL)
		Panic(0, "%s", strnomem);

	return cp;
}

char *SaveStrn(const char *str, size_t n)
{
	char *cp = strndup(str, n + 1);

	if (cp == NULL)
		Panic(0, "%s", strnomem);

	return cp;
}

void centerline(char *str, int y)
{
	int l, n;

	n = strlen(str);
	if (n > flayer->l_width - 1)
		n = flayer->l_width - 1;
	l = (flayer->l_width - 1 - n) / 2;
	LPutStr(flayer, str, n, &mchar_blank, l, y);
}

void leftline(char *str, int y, struct mchar *rend)
{
	int l, n;
	struct mchar mchar_dol;

	mchar_dol = mchar_blank;
	mchar_dol.image = '$';

	l = n = strlen(str);
	if (n > flayer->l_width - 1)
		n = flayer->l_width - 1;
	LPutStr(flayer, str, n, rend ? rend : &mchar_blank, 0, y);
	if (n != l)
		LPutChar(flayer, &mchar_dol, n, y);
}

char *Filename(char *s)
{
	char *p = s;

	if (p)
		while (*p)
			if (*p++ == '/')
				s = p;
	return s;
}

char *stripdev(char *name)
{
	if (name == NULL)
		return NULL;
	if (strncmp(name, "/dev/", 5) == 0)
		return name + 5;
	return name;
}

/*
 *    Signal handling
 */

void (*xsignal(int sig, void (*func) (int))) (int) {
	struct sigaction osa, sa;
	sa.sa_handler = func;
	(void)sigemptyset(&sa.sa_mask);
#ifdef SA_RESTART
	sa.sa_flags = (sig == SIGCHLD ? SA_RESTART : 0);
#else
	sa.sa_flags = 0;
#endif
	if (sigaction(sig, &sa, &osa))
		return (void (*)(int))-1;
	return osa.sa_handler;
}

/*
 *    uid/gid handling
 */

void xseteuid(int euid)
{
	if (seteuid(euid) == 0)
		return;
	if (seteuid(0) || seteuid(euid))
		Panic(errno, "seteuid");
}

void xsetegid(int egid)
{
	if (setegid(egid))
		Panic(errno, "setegid");
}

void Kill(pid_t pid, int sig)
{
	if (pid < 2)
		return;
	(void)kill(pid, sig);
}

void closeallfiles(int except)
{
	struct pollfd pfd[1024];
	int maxfd, i, fd, ret, z;

	i = 3; /* skip stdin, stdout and stderr */
	maxfd = getdtablesize();

	while (i < maxfd) {
		memset(pfd, 0, sizeof(pfd));

		z = 0;
		for (fd = i; fd < maxfd && fd < i + 1024; fd++)
			pfd[z++].fd = fd;

		ret = poll(pfd, fd - i, 0);
		if (ret < 0)
			Panic(errno, "poll");

		z = 0;
		for (fd = i; fd < maxfd && fd < i + 1024; fd++)
			if (!(pfd[z++].revents & POLLNVAL) && fd != except)
				close(fd);

		i = fd;
	}
}

/*
 *  Security - switch to real uid
 */

static int UserSTAT;

int UserContext(void)
{
	xseteuid(real_uid);
	xsetegid(real_gid);
	return 1;
}

void UserReturn(int val)
{
	xseteuid(eff_uid);
	xsetegid(eff_gid);
	UserSTAT = val;
}

int UserStatus(void)
{
	return UserSTAT;
}

int AddXChar(char *buf, int ch)
{
	char *p = buf;

	if (ch < ' ' || ch == 0x7f) {
		*p++ = '^';
		*p++ = ch ^ 0x40;
	} else if (ch >= 0x80) {
		*p++ = '\\';
		*p++ = (ch >> 6 & 7) + '0';
		*p++ = (ch >> 3 & 7) + '0';
		*p++ = (ch >> 0 & 7) + '0';
	} else
		*p++ = ch;
	return p - buf;
}

int AddXChars(char *buf, int len, char *str)
{
	char *p;

	if (str == NULL) {
		*buf = 0;
		return 0;
	}
	len -= 4;		/* longest sequence produced by AddXChar() */
	for (p = buf; p < buf + len && *str; str++) {
		if (*str == ' ')
			*p++ = *str;
		else
			p += AddXChar(p, *str);
	}
	*p = 0;
	return p - buf;
}

static time_t GetBootTime(void)
{
	char statbuf[128], *endptr;
	FILE *fp;
	long boot_l;
	time_t boot_time;

	fp = fopen("/proc/stat", "r");
	if (fp == NULL)
		return 0;
	while (fgets(statbuf, sizeof(statbuf), fp)) {
		if (strncmp(statbuf, "btime ", 6) != 0)
			continue;
		errno = 0;
		boot_l = strtol(statbuf + 6, &endptr, 10);
		fclose(fp);
		if (errno == ERANGE || endptr == statbuf + 6)
			return 0;
		if (*endptr != '\0' && *endptr != '\n' && *endptr != ' ' && *endptr != '\t')
			return 0;
		if (boot_l <= 0)
			return 0;
		boot_time = (time_t)boot_l;
		if ((long)boot_time != boot_l)
			return 0;
		return boot_time;
	}
	fclose(fp);
	return 0;
}

time_t SessionCreationTime(const char *fifo)
{
	char ppath[32], pdata[512];
	int pfd, field;
	ssize_t rr;
	char *jiffies, *endptr;
	long pid_l;
	unsigned long long start_time;
	long clock_ticks;
	time_t boot_time, created;

	errno = 0;
	pid_l = strtol(fifo, &endptr, 10);
	if (errno == ERANGE || endptr == fifo)
		return 0;
	if (*endptr != '\0' && *endptr != '.')
		return 0;
	if (pid_l <= 0)
		return 0;
	snprintf(ppath, sizeof(ppath), "/proc/%ld/stat", pid_l);
	pfd = open(ppath, O_RDONLY);
	if (pfd < 0)
		return 0;
	rr = read(pfd, pdata, sizeof(pdata) - 1);
	close(pfd);
	if (rr <= 0)
		return 0;
	pdata[rr] = '\0';

	jiffies = strrchr(pdata, ')');
	if (jiffies == NULL || jiffies[1] != ' ' || jiffies[2] == '\0')
		return 0;
	jiffies += 3;
	jiffies = strchr(jiffies, ' ');
	if (jiffies == NULL || jiffies[1] == '\0')
		return 0;
	jiffies++;
	for (field = 4; field < 22; field++) {
		jiffies = strchr(jiffies, ' ');
		if (jiffies == NULL || jiffies[1] == '\0')
			return 0;
		jiffies++;
	}
	errno = 0;
	start_time = strtoull(jiffies, &endptr, 10);
	if (errno == ERANGE || endptr == jiffies)
		return 0;
	if (*endptr != '\0' && *endptr != '\n' && *endptr != ' ' && *endptr != '\t')
		return 0;

	clock_ticks = sysconf(_SC_CLK_TCK);
	if (clock_ticks <= 0)
		return 0;
	boot_time = GetBootTime();
	if (boot_time <= 0)
		return 0;
	start_time = (unsigned long long)boot_time + start_time / (unsigned long long)clock_ticks;
	if (start_time < (unsigned long long)boot_time)
		return 0;
	created = (time_t)start_time;
	if (created <= 0 || (unsigned long long)created != start_time)
		return 0;
	return created;
}
