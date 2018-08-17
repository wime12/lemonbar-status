#include <sys/stat.h>

#include <string.h>
#include <paths.h>
#include <unistd.h>
#include <err.h>
#include <fcntl.h>

#include "colors.h"

#define MAIL_TEXT "MAIL"
#define MAILPATH_BUFLEN 256

static int timespec_later(struct timespec *, struct timespec *);

int
mail_init()
{
	char mail_path[MAILPATH_BUFLEN];
	char *user;
	int mail_fd = -1;

	strlcpy(mail_path, _PATH_MAILDIR "/", MAILPATH_BUFLEN);

	if ((user = getlogin()) == NULL) {
		warn("cannot get user's login name");
		return mail_fd;
	}

	strlcat(mail_path, user, MAILPATH_BUFLEN);

	if ((mail_fd = open(mail_path, O_RDONLY)) < 0)
		warn("cannot open %s", mail_path);

	return mail_fd;
}

char *
mail_info(int fd)
{
    	struct stat st;

	if (fd < 0) {
	    	warn("invalid mail file descriptor");
		return NULL;
	}

	if (fstat(fd, &st) < 0) {
		warn("cannot get mail box status");
		return NULL;
	}

	if (timespec_later(&st.st_mtim, &st.st_atim))
		return MAIL_COLOR MAIL_TEXT NORMAL_COLOR;
	else
		return NULL;
}


/* Was t1 later than t2? */
static int
timespec_later(struct timespec *t1, struct timespec *t2)
{
	if (t1->tv_sec == t2->tv_sec)
		return t1->tv_nsec > t2->tv_nsec;
	else
		return t1->tv_sec > t2->tv_sec;
}

