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

#include <err.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "audio.h"
#include "battery.h"
#include "clock.h"
#include "colors.h"
#include "mail.h"
#include "mpd.h"
#include "net.h"
#include "weather.h"
#include "x.h"

#define EVENTS 10

enum infos { INFO_MPD, INFO_MAIL, INFO_NETWORK, INFO_BATTERY,
    INFO_BRIGHTNESS, INFO_AUDIO, INFO_WEATHER, INFO_CLOCK,
    INFO_ARRAY_SIZE };

#define LEFT_ALIGNED INFO_MPD

enum timer_ids { CLOCK_TIMER, BATTERY_TIMER, NET_TIMER,
    BRIGHTNESS_TIMER, AUDIO_TIMER };

static void	output_elements(char **, int, int);
static void	output_status(char **);

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

int
main()
{
	char *infos[INFO_ARRAY_SIZE], c;
	struct kevent kev_in[EVENTS], kev[EVENTS];
	int kq, nev, i, mail_fd, weather_fd, clock_update, n, pipe_fd[2],
            mpd_fd;
	
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

                if (audio_init()) {
                        infos[INFO_AUDIO] = audio_info();

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
					    audio_info();
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
						    audio_info();
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
