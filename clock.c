#include <sys/types.h>
#include <time.h>
#include <err.h>

#define CLOCK_FORMAT "%a %b %d, %R"
#define CLOCK_BUFLEN 18

char *
clock_info(int *next_update)
{
	static char str[CLOCK_BUFLEN];
	struct tm ltime;
	time_t clock;

	if (next_update)
	    *next_update = 10 * 1000;

	if ((clock = time(NULL)) < 0) {
		warn("cannot get time");
		return NULL;
	}

	if (localtime_r(&clock, &ltime) == NULL) {
		warn("cannot convert to localtime");
		return NULL;
	}

	if (next_update)
		*next_update = (60 - ltime.tm_sec) * 1000;

	strftime(str, sizeof(str), CLOCK_FORMAT, &ltime);

	return str;
}

