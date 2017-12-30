/*
 * lemonbar-status -- formats the system status for lemonbar
 *
 *
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
 */

#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <machine/apmvar.h>
/* room for net includes */

#include <unistd.h>
#include <errno.h>
#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

#define BATT_INFO_BUFLEN 12
#define APM_DEV_PATH "/dev/apm"

static int	open_socket(const char *);
static void	get_battery_info(struct apm_power_info *);
static void	format_battery_info(char *, size_t, struct apm_power_info *);


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

static void
get_battery_info(struct apm_power_info *info)
{
	int fd, state;

	fd = open(APM_DEV_PATH, O_RDONLY);
	if (fd == -1)
		err(1, "cannot open " APM_DEV_PATH);

	state = ioctl(fd, APM_IOC_GETPOWER, info);
	close(fd);

	if (state < 0) err(1, "cannot read battery info");
}

static void
format_battery_info(char *str, size_t len, struct apm_power_info *info)
{
	int minutes, n;

	n = -1;
	switch (info->ac_state) {
	case APM_AC_OFF:
	        minutes = info->minutes_left;
		if (minutes < 0)
			n = strlcpy(str, "??:??", len);
		else
			n = snprintf(str, len, "%d:%02d",
			    minutes / 60, minutes % 60);
		/* FALLTHROUGH */
	case APM_AC_ON:
		if (n < 0)
			n = strlcpy(str, "A/C", len);

		str += n;
		snprintf(str, len - n, " (%d%%)", info->battery_life);
		break;
	default:
		snprintf(str, len, "???");
	}
}


/* Network */

int
main() {
    	struct apm_power_info batt_info;
	char batt_string[BATT_INFO_BUFLEN];

	get_battery_info(&batt_info);
	format_battery_info(batt_string, BATT_INFO_BUFLEN, &batt_info);
	puts(batt_string);

	return 0;
}

/* vim:sw=4:
*/
