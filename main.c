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
 * which can be processes by lemonbar.
 *
 * The program does not take any commandline arguments and is not
 * configurable.
 *
 * If it is appropriate, the program waits for events from the information
 * sources. Otherwise the information is polled at regular intervals.
 */

#include <sys/types.h>
#include <sys/event.h>
#include <sys/ioctl.h>
#include <sys/audioio.h>
#include <sys/stat.h>

#include <errno.h>
#include <err.h>
#include <fcntl.h>
#include <inttypes.h>
#include <paths.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#include "colors.h"
#include "mpd.h"
#include "mail.h"
#include "clock.h"
#include "battery.h"
#include "net.h"
#include "weather.h"
#include "x.h"

#define MIXER_DEV_PATH "/dev/mixer"
#define MIXER_DEVICE_CLASS "outputs"
#define MIXER_DEVICE "master"
#define MIXER_MUTE_DEVICE "mute"

#define AUDIO_BUFLEN 8
#define AUDIO_INTERVAL (10 * 1000)

enum infos { INFO_MPD, INFO_MAIL, INFO_NETWORK, INFO_BATTERY,
    INFO_BRIGHTNESS, INFO_AUDIO, INFO_WEATHER, INFO_CLOCK,
    INFO_ARRAY_SIZE };

#define LEFT_ALIGNED INFO_MPD

enum timer_ids { CLOCK_TIMER, BATTERY_TIMER, NET_TIMER,
    BRIGHTNESS_TIMER, AUDIO_TIMER };

char	       *audio_info(int, int);
int		audio_init(int *, int *);
int		audio_print_volume(char *, size_t, int);
static void	output_status(char **);

/* Audio */

int
audio_init(int *mixer_device_index, int *mute_device_index)
{
	struct mixer_devinfo devinfo;
	int fd, class_index, ret;

	ret = 0;
	class_index = *mixer_device_index = *mute_device_index = -1;

	fd = open(MIXER_DEV_PATH, O_RDONLY);
	if (fd == -1) {
		warn("cannot open " MIXER_DEV_PATH);
		goto cleanup_1;
	}

	for (devinfo.index = 0;
	    ioctl(fd, AUDIO_MIXER_DEVINFO, &devinfo) != -1;
	    devinfo.index++) {
		if (strncmp(MIXER_DEVICE_CLASS, devinfo.label.name,
		    sizeof(MIXER_DEVICE_CLASS)) == 0 &&
		    devinfo.type == AUDIO_MIXER_CLASS) {
			class_index = devinfo.index;
			break;
		}
	}
	if (class_index == -1) {
		warnx("mixer device class " MIXER_DEVICE_CLASS
		    " not found");
		goto cleanup_2;
	}

	for (devinfo.index = 0;
	    ioctl(fd, AUDIO_MIXER_DEVINFO, &devinfo) != -1;
	    devinfo.index++) {
		if (strncmp(MIXER_DEVICE, devinfo.label.name,
		    sizeof(MIXER_DEVICE)) == 0 &&
		    devinfo.type == AUDIO_MIXER_VALUE) {
			*mixer_device_index = devinfo.index;
			break;
		}
	}
	if (*mixer_device_index == -1) {
		warnx("mixer device " MIXER_DEVICE_CLASS "."
		    MIXER_DEVICE " not found");
		goto cleanup_2;
	}

	for (devinfo.index = devinfo.next;
	    devinfo.next != AUDIO_MIXER_LAST &&
	    ioctl(fd, AUDIO_MIXER_DEVINFO, &devinfo) != -1;
	    devinfo.index = devinfo.next) {
		if (strncmp(MIXER_MUTE_DEVICE, devinfo.label.name,
		    sizeof(MIXER_MUTE_DEVICE)) == 0 &&
		    devinfo.type == AUDIO_MIXER_ENUM) {
			*mute_device_index = devinfo.index;
			break;
		}
	}
	if (*mute_device_index == -1) {
		warnx("mute device " MIXER_DEVICE_CLASS "."
		    MIXER_DEVICE "." MIXER_MUTE_DEVICE " not found");
		goto cleanup_2;
	}

	ret = 1;

cleanup_2:
	close(fd);

cleanup_1:
	return ret;
}

char *
audio_info(int mixer_device, int mute_device)
{
	static char str[AUDIO_BUFLEN], *res, *strp;
	int fd, n, muted;
	size_t buflen;
	int left, right;
	mixer_ctrl_t value;

	res = NULL;
	left = right = -1;

	fd = open(MIXER_DEV_PATH, O_RDONLY);
	if (fd == -1) {
		warn("cannot open " MIXER_DEV_PATH);
		goto cleanup_1;
	}

	value.dev = mute_device;
	value.type = AUDIO_MIXER_ENUM;
	if (ioctl(fd, AUDIO_MIXER_READ, &value) < 0) {
		warn("cannot get mixer mute state");
		goto cleanup_2;
	}
	muted = value.un.ord;

	if (!muted) {
		value.dev = mixer_device;
		value.type = AUDIO_MIXER_VALUE;
		value.un.value.num_channels = 2;
		if (ioctl(fd, AUDIO_MIXER_READ, &value) < 0) {
			warn("cannot get mixer values");
			goto cleanup_2;
		}
		left = (int)value.un.value.level[0];
		right = (int)value.un.value.level[1];
	}

	strp = str;
	buflen = sizeof(str);

	n = audio_print_volume(strp, buflen, left);
	strp += n;
	buflen -= n;

	n = strlcpy(strp, ":", buflen);
	strp += n;
	buflen -= n;

	n = audio_print_volume(strp, buflen, right);

	res = str;

cleanup_2:
	close(fd);

cleanup_1:
	return res;
}

int
audio_print_volume(char *str, size_t buflen, int vol)
{
	if (vol < AUDIO_MIN_GAIN)
		return strlcpy(str, "_", buflen);
	else if (vol >= AUDIO_MAX_GAIN)
		return strlcpy(str, "M", buflen);
	else
		return snprintf(str, buflen, "%d",
		    (int)(vol / ((AUDIO_MAX_GAIN - AUDIO_MIN_GAIN)
			/ 100.0)));
}

static void
output_elements(char *infos[], int start, int end)
{
        int i;

	for (i = start; i < end; i++) {
		if (infos[i] == NULL)
			continue;
		fputs(infos[i], stdout);
		i++;
		break;
	}

	for (; i < end; i++) {
		if (infos[i] == NULL)
			continue;
		fputs(" " SEPARATOR_COLOR "|" NORMAL_COLOR " ", stdout);
		fputs(infos[i], stdout);
	}
}

static void
output_status(char *infos[])
{
	int i;

        /* search first left aligned element */
        for (i = 0; i < INFO_ARRAY_SIZE && infos[i] == NULL; i++)
                ;

        if (i <= LEFT_ALIGNED) {
                fputs(NORMAL_COLOR "%{l}", stdout);
                output_elements(infos, i, LEFT_ALIGNED + 1);
                i++;
        }

        /* search first right aligned element */
        for (; i < INFO_ARRAY_SIZE && infos[i] == NULL; i++)
                ;

        /* if first right aligned element found */
        if (i < INFO_ARRAY_SIZE) {
                fputs(NORMAL_COLOR "%{r}", stdout);
                output_elements(infos, i, INFO_ARRAY_SIZE);
        }

	putc('\n', stdout);
	fflush(stdout);
}

#define EVENTS 10

int
main()
{
	char *infos[INFO_ARRAY_SIZE], c;
	struct kevent kev_in[EVENTS], kev[EVENTS];
	int mail_fd, kq, nev, i,
	    weather_fd, clock_update, n, mixer_device, mute_device,
	    randr_event_base, pipe_fd[2], mpd_fd;
	
	bzero(infos, INFO_ARRAY_SIZE * sizeof(char *));
	n = 0;

        /* Mail */

	if ((mail_fd = mail_init()) >= 0) {
                infos[INFO_MAIL] = mail_info(mail_fd);
		EV_SET(&kev_in[n++], mail_fd, EVFILT_VNODE, EV_ADD |
		    EV_CLEAR, NOTE_WRITE | NOTE_EXTEND | NOTE_ATTRIB,
		    0, NULL);
	}

        /* MPD */

        if ((mpd_fd = mpd_init()) >= 0) {
                infos[INFO_MPD] = mpd_info(mpd_fd);
                mpd_idle_start(mpd_fd);
		EV_SET(&kev_in[n++], mpd_fd, EVFILT_READ, EV_ADD |
		    EV_CLEAR, 0, 0, NULL);
        }
        
        /* Weather */

        if ((weather_fd = weather_init()) >= 0) {
                infos[INFO_WEATHER] = weather_info();
		EV_SET(&kev_in[n++], weather_fd, EVFILT_VNODE,
		    EV_ADD | EV_CLEAR, NOTE_WRITE, 0, NULL);
        }

        if (pipe(pipe_fd) == -1) {
                warn("could not open pipe");
        } else {

                /* Brightness */

                if (x_init(pipe_fd[1])) {

                        infos[INFO_BRIGHTNESS] = x_info();

                        EV_SET(&kev_in[n++], BRIGHTNESS_TIMER, EVFILT_TIMER,
                            EV_ADD, 0, BRIGHTNESS_INTERVAL, NULL);
                        EV_SET(&kev_in[n++], pipe_fd[0], EVFILT_READ,
                                EV_ADD, 0, 0, NULL);
                }

                /* Audio */

                if (audio_init(&mixer_device, &mute_device)) {
                        infos[INFO_AUDIO] = audio_info(mixer_device,
                            mute_device);

                        EV_SET(&kev_in[n++], AUDIO_TIMER, EVFILT_TIMER,
                            EV_ADD, 0, AUDIO_INTERVAL, NULL);
                        EV_SET(&kev_in[n++], pipe_fd[0], EVFILT_READ,
                            EV_ADD, 0, 0, NULL);
                }
        }

        /* Clock */

	infos[INFO_CLOCK] = clock_info(&clock_update);

	EV_SET(&kev_in[n++], CLOCK_TIMER, EVFILT_TIMER, EV_ADD, 0,
	    clock_update, NULL);

        /* Battery */

	infos[INFO_BATTERY] = battery_info();

	EV_SET(&kev_in[n++], BATTERY_TIMER, EVFILT_TIMER, EV_ADD, 0,
	    BATTERY_INTERVAL, NULL);

        /* Network */

	infos[INFO_NETWORK] = net_info();

	EV_SET(&kev_in[n++], NET_TIMER, EVFILT_TIMER, EV_ADD, 0,
	    NET_INTERVAL, NULL);

        /* Event Loop */

	output_status(infos);

	if ((kq = kqueue()) < 0)
		err(1, "cannot create kqueue");

	for (;;) {
		nev = kevent(kq, kev_in, n, kev, EVENTS, NULL);
		n = 0;
		if (nev == -1)
			err(1, NULL);

		if (nev == 0)
			continue;

		for (i = 0; i < nev; i++) {

			if (kev[i].flags & EV_ERROR)
				errx(1, "%s",
				    strerror(kev[i].data));

			switch (kev[i].filter) {

			case EVFILT_VNODE:

				if (kev[i].ident == (uintptr_t)mail_fd)
					infos[INFO_MAIL] =
					    mail_info(mail_fd);
				else if (kev[i].ident ==
				    (uintptr_t)weather_fd)
					infos[INFO_WEATHER] =
					    weather_info();
				break;

			case EVFILT_TIMER:

				switch (kev[i].ident) {

				case CLOCK_TIMER:
					infos[INFO_CLOCK] =
						clock_info(&clock_update);
					EV_SET(&kev_in[n++], CLOCK_TIMER,
					    EVFILT_TIMER, EV_DELETE, 0, 0,
					    NULL);
					EV_SET(&kev_in[n++],
					    CLOCK_TIMER, EVFILT_TIMER,
					    EV_ADD, 0, clock_update, NULL);
					break;

				case BATTERY_TIMER:
					infos[INFO_BATTERY] =
					    battery_info();
					break;

				case NET_TIMER:
					infos[INFO_NETWORK] =
					    net_info();
					break;

				case BRIGHTNESS_TIMER:
					infos[INFO_BRIGHTNESS] = x_info();
					break;
				case AUDIO_TIMER:
					infos[INFO_AUDIO] =
					    audio_info( mixer_device,
						mute_device);
					break;
				}
				break;

			case EVFILT_READ:
				if (kev[i].ident ==
				    (uintptr_t)pipe_fd[0]) {
					read(pipe_fd[0], &c, 1);
					switch (c) {
					case BRIGHTNESS_EVENT:
						infos[INFO_BRIGHTNESS] =
						    x_info();
						break;
					case AUDIO_EVENT:
						infos[INFO_AUDIO] =
						    audio_info(
						    mixer_device,
						    mute_device);
						break;
					}
				} else if (kev[i].ident ==
                                    (uintptr_t)mpd_fd) {
                                            mpd_idle_end(mpd_fd);
                                            infos[INFO_MPD] =
                                                mpd_info(mpd_fd);
                                            mpd_idle_start(mpd_fd);
                                }

				break;
			}
		}
		output_status(infos);
	}

cleanup_1:

	return 0;
}

/* vim:sw=4:
*/
