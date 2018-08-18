#include <sys/types.h>
#include <sys/ioctl.h>
#include <machine/apmvar.h>
#include <fcntl.h>
#include <err.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

#define BATT_INFO_BUFLEN 13
#define APM_DEV_PATH "/dev/apm"

char *
battery_info()
{
	struct apm_power_info info;
	static char str[BATT_INFO_BUFLEN];
	int minutes, n, fd, state;

	fd = open(APM_DEV_PATH, O_RDONLY);
	if (fd == -1) {
		warn("cannot open " APM_DEV_PATH);
		return NULL;
	}

	state = ioctl(fd, APM_IOC_GETPOWER, &info);
	close(fd);

	if (state < 0) {
		warn("cannot read battery info");
		return NULL;
	}

	n = -1;
	switch (info.ac_state) {

	case APM_AC_OFF:
	        minutes = info.minutes_left;
		if (minutes < 0)
			n = strlcpy(str, "--:--", BATT_INFO_BUFLEN);
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
