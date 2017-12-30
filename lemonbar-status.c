/*
 * Copyright (c) 2017 Wilfried Meindl <wilfried.meindl@gmail.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 *
 * lemonbar-status -- formats the system status for lemonbar
 *
 * This program collects information about the user's mail status,
 * the net connection, the battery status, the screen brightness,
 * the weather, and the date and outputs a line on standard ouput
 * which can be processes by lemonbar. It tries to be low on system
 * resources.
 *
 * The program does not take any commandline arguments.
 *
 * If it is appropriate, the program waits for events from the information
 * sources. Otherwise the information is polled at regular intervals.
 */


#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <machine/apmvar.h>
/* room for net includes */

#include <errno.h>
#include <err.h>
#include <fcntl.h>
#include <paths.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>


#define BATT_INFO_BUFLEN 13
#define MAILPATH_BUFLEN 256
#define DATE_BUFLEN 18
#define APM_DEV_PATH "/dev/apm"
#define NORMAL_COLOR "%{F#DDDDDD}"
#define MAIL_COLOR "%{F#FFFF00}"


static int	open_socket(const char *);
static char    *battery_info();
static int	timespec_later(struct timespec *, struct timespec *);
static int	mail_file();
static char    *mail_info(int fd);
static char    *clock_info();
static void	output_status();

enum infos { INFO_MAIL, INFO_BATTERY, INFO_CLOCK, INFO_ARRAY_SIZE };


static int
open_socket(const char *sockname)
{
	struct sockaddr_un s_un;
	int errr, sock;

	sock = socket(AF_UNIX, SOCK_STREAM, 0);
	if (sock == -1)
		err(1, "cannot create local socket");

	s_un.sun_family = AF_UNIX;
	strlcpy(s_un.sun_path, sockname, sizeof(s_un.sun_path));
	if (connect(sock, (struct sockaddr *)&s_un, sizeof(s_un)) == -1) {
		errr = errno;
		close(sock);
		errno = errr;
		sock = -1;
	}
	return (sock);
}


/* Battery */

static char *
battery_info()
{
	struct apm_power_info info;
	static char str[BATT_INFO_BUFLEN];
	int minutes, n, fd, state;

	fd = open(APM_DEV_PATH, O_RDONLY);
	if (fd == -1)
		err(1, "cannot open " APM_DEV_PATH);

	state = ioctl(fd, APM_IOC_GETPOWER, &info);
	close(fd);

	if (state < 0) err(1, "cannot read battery info");

	n = -1;
	switch (info.ac_state) {
	case APM_AC_OFF:
	        minutes = info.minutes_left;
		if (minutes < 0)
			n = strlcpy(str, "??:??", BATT_INFO_BUFLEN);
		else
			n = snprintf(str, BATT_INFO_BUFLEN, "%d:%02d",
			    minutes / 60, minutes % 60);
		/* FALLTHROUGH */
	case APM_AC_ON:
		if (n < 0)
			n = strlcpy(str, "A/C", BATT_INFO_BUFLEN);

		snprintf(str + n, BATT_INFO_BUFLEN - n, " (%d%%)",
		    info.battery_life);
		return str;
		break;
	default:
		return NULL;
	}
}


/* Mail */

/* Was t1 later than t2? */
static int
timespec_later(struct timespec *t1, struct timespec *t2)
{
	if (t1->tv_sec == t2->tv_sec)
		return t1->tv_nsec > t2->tv_nsec;
	else
		return t1->tv_sec > t2->tv_sec;
}

static int
mail_file()
{
	char mail_path[MAILPATH_BUFLEN];
	char *user;
	int mail_fd;

	strlcpy(mail_path, _PATH_MAILDIR "/", MAILPATH_BUFLEN);

	if ((user = getlogin()) == NULL)
	    err(1, "cannot get login name");

	strlcat(mail_path, user, MAILPATH_BUFLEN);

	if ((mail_fd = open(mail_path, O_RDONLY)) < 0)
		err(1, "cannot open %s", mail_path);

	return mail_fd;
}

static char *
mail_info(int fd)
{
    	struct stat mail_info;

	if (fstat(fd, &mail_info) < 0)
		err(1, "cannot get mail box status");

	if (timespec_later(&mail_info.st_mtim, &mail_info.st_atim))
		return MAIL_COLOR "MAIL" NORMAL_COLOR;
	else
		return NULL;
}


/* Clock */

static char *
clock_info()
{
	static char str[DATE_BUFLEN];
	struct tm ltime;
	time_t clock;

	if ((clock = time(NULL)) < 0)
		err(1, "cannot get time");

	if (localtime_r(&clock, &ltime) == NULL)
		err(1, "cannot convert to localtime");
	strftime(str, DATE_BUFLEN, "%a %b %d, %R", &ltime);

	return str;
}

static void
output_status(char *infos[], int len)
{
	int i;

	for (i = 0; i < len; i++) {
		if (infos[i] == NULL)
			continue;
		fputs(infos[i], stdout);
		if (i < len - 1)
			fputs(" | ", stdout);
	}
	fputs("\n", stdout);
}


int
main()
{
	char *mail_msg, *infos[INFO_ARRAY_SIZE];
	int mail_fd;

	mail_fd = mail_file();

	/* Battery */
	infos[INFO_BATTERY] = battery_info();

	/* Mail */
	mail_msg = mail_info(mail_fd);
	close(mail_fd);
	infos[INFO_MAIL] = mail_msg;

	/* Clock */
	infos[INFO_CLOCK] = clock_info();

	output_status(infos, INFO_ARRAY_SIZE);

	return 0;
}

/* vim:sw=4:
*/
