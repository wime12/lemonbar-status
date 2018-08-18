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

#include <xcb/xcb.h>
#include <xcb/randr.h>

#include <json-c/json.h>

#include "colors.h"
#include "mpd.h"
#include "mail.h"
#include "clock.h"
#include "battery.h"
#include "net.h"


/* TODO: Do not hardcode the home directory. */
#define MIXER_DEV_PATH "/dev/mixer"
#define MIXER_DEVICE_CLASS "outputs"
#define MIXER_DEVICE "master"
#define MIXER_MUTE_DEVICE "mute"
#define DATE_FORMAT "%a %b %d, %R"
#define OUTPUT_NAME "eDP1"
#define WEATHER_CURRENT_FILENAME "/home/wilfried/.cache/weather/current"
#define WEATHER_TIMESTAMP_FILENAME "/home/wilfried/.cache/weather/timestamp"

#define AUDIO_MUTE_KEYCODE 160
#define AUDIO_DOWN_KEYCODE 174
#define AUDIO_UP_KEYCODE 176

#define DATE_BUFLEN 18
#define BRIGHTNESS_BUFLEN 5
#define WEATHER_BUFLEN 48
#define AUDIO_BUFLEN 8

#define BATTERY_INTERVAL (10 * 1000)
#define NET_INTERVAL (10 * 1000)
#define BRIGHTNESS_INTERVAL (10 * 1000)
#define AUDIO_INTERVAL (10 * 1000)


enum infos { INFO_MPD, INFO_MAIL, INFO_NETWORK, INFO_BATTERY,
    INFO_BRIGHTNESS, INFO_AUDIO, INFO_WEATHER, INFO_CLOCK,
    INFO_ARRAY_SIZE };

#define LEFT_ALIGNED INFO_MPD

enum timer_ids { CLOCK_TIMER, BATTERY_TIMER, NET_TIMER,
    BRIGHTNESS_TIMER, AUDIO_TIMER };

struct x_event_loop_args
{
	xcb_connection_t *conn;
	xcb_window_t root;
	int event_base;
	int out;
};

char	       *audio_info(int, int);
int		audio_init(int *, int *);
int		audio_print_volume(char *, size_t, int);
void		x_event_loop(xcb_connection_t *, xcb_window_t, int, int);
void	       *x_event_loop_thread_start(struct x_event_loop_args *);
static char    *brightness_info(xcb_connection_t *, xcb_randr_output_t,
    xcb_atom_t, int);
int		brightness_init(xcb_connection_t **, xcb_window_t *,
    xcb_atom_t *, xcb_randr_output_t *, int *, int *);
static void	output_status(char **);
int		weather_file();
char	       *weather_info();

/* Brightness */

/* TODO: rename brightness_init() to x_init() */

int
brightness_init(xcb_connection_t **conn_out, xcb_window_t *root_out,
    xcb_atom_t *backlight_atom_out, xcb_randr_output_t *output_out,
    int *randr_event_base, int *range_out)
{
	xcb_generic_error_t *error = NULL;
	xcb_connection_t *conn = NULL;
	const xcb_query_extension_reply_t *randr_data;
	xcb_randr_query_version_reply_t *ver_reply = NULL;
	xcb_intern_atom_reply_t *backlight_reply = NULL;
	xcb_atom_t backlight_atom;
	xcb_screen_t *screen = NULL;
	xcb_window_t root = { 0 };
	xcb_screen_iterator_t iter;
	xcb_randr_get_screen_resources_reply_t *resources_reply = NULL;
	xcb_randr_output_t *outputs = NULL;
	xcb_randr_get_output_info_reply_t *output_info_reply = NULL;
	xcb_randr_query_output_property_reply_t *prop_query_reply = NULL;
	int default_screen, i, res = 0;
	int32_t *limits;

	conn = xcb_connect(NULL, &default_screen);
	if (xcb_connection_has_error(conn)) {
		warnx("cannot connect do display");
		xcb_disconnect(conn);
		conn = NULL;
		goto cleanup_1;
	}
	*conn_out = conn;

	randr_data = xcb_get_extension_data(conn, &xcb_randr_id);
	if (!randr_data->present) {
		warnx("cannot find RandR extension");
		goto cleanup_2;
	}
	if (randr_event_base)
		*randr_event_base = randr_data->first_event;

	ver_reply = xcb_randr_query_version_reply(conn,
		xcb_randr_query_version(conn, 1, 2), &error);
	if (error != NULL || ver_reply == NULL) {
		warnx("cannot query RandR version");
		goto cleanup_1;
	}
	if (ver_reply->major_version != 1 || ver_reply->minor_version < 2) {
		warnx("RandR version %d.%d is too old",
		    ver_reply->major_version, ver_reply->minor_version);
		goto cleanup_2;
	}

	backlight_reply = xcb_intern_atom_reply(conn,
	    xcb_intern_atom(conn, 1, strlen("Backlight"), "Backlight"),
	    &error);
	if (error != NULL || backlight_reply == NULL) {
		warnx("cannot intern backlight atom");
		goto cleanup_2;
	}
        backlight_atom = backlight_reply->atom;

	if (backlight_atom == XCB_NONE) {
		warnx("no outputs have backlight property");
		goto cleanup_3;
	}
	*backlight_atom_out = backlight_atom;

	iter = xcb_setup_roots_iterator(xcb_get_setup(conn));
	i = default_screen;
	for (; iter.rem; --i, xcb_screen_next(&iter))
		if (i == 0)
			screen = iter.data;
	if (!screen) {
		warnx("no screen found");
		goto cleanup_3;
	}
	if (!screen->root) {
		warnx("no root window found");
		goto cleanup_3;
	}
	*root_out = root = screen->root;

	resources_reply = xcb_randr_get_screen_resources_reply(conn,
	    xcb_randr_get_screen_resources(conn, root), &error);
	if (error != NULL || resources_reply == NULL) {
		warnx("cannot get screen resources");
		goto cleanup_3;
	}

	outputs = xcb_randr_get_screen_resources_outputs(resources_reply);
	for (i = 0; i < resources_reply->num_outputs; i++) {
		output_info_reply = xcb_randr_get_output_info_reply(conn,
			xcb_randr_get_output_info(conn, outputs[i],
			    resources_reply->timestamp), &error);
		if (error != NULL || output_info_reply == NULL) {
			warnx("cannot get output name");
			goto cleanup_4;
		}
		if (strncmp(OUTPUT_NAME,
		    xcb_randr_get_output_info_name(output_info_reply),
		    xcb_randr_get_output_info_name_length(output_info_reply))
		    != 0) {
			free(output_info_reply);
			output_info_reply = NULL;
			continue;
		}
		break;
	}
	if (output_info_reply == NULL) {
	    warn("RandR output " OUTPUT_NAME "not found");
	    goto cleanup_4;
	}
	*output_out = outputs[i];

	prop_query_reply =
	    xcb_randr_query_output_property_reply(conn,
		xcb_randr_query_output_property(conn, outputs[i],
		    backlight_atom), &error);
	if (error != NULL || prop_query_reply == NULL) {
		warnx("cannot query brightness limit propery");
		goto cleanup_4;
	}
	if (prop_query_reply->range == 0 ||
	    xcb_randr_query_output_property_valid_values_length(
		prop_query_reply) != 2) {
		warnx("could not get brightness min and max values");
		goto cleanup_5;
	}
	limits = xcb_randr_query_output_property_valid_values(
	    prop_query_reply);
	*range_out = limits[1] - limits[0];

	res = 1;

cleanup_5:
	free(prop_query_reply);
	
cleanup_4:
	free(resources_reply);

cleanup_3:
	free(backlight_reply);

cleanup_2:
	free(ver_reply);

cleanup_1:
	if (!res)
		xcb_disconnect(conn);
	return res;
}


char *
brightness_info(xcb_connection_t *conn, xcb_randr_output_t output,
    xcb_atom_t backlight_atom, int range)
{
    	static char str[BRIGHTNESS_BUFLEN];
	xcb_generic_error_t *error = NULL;
	xcb_randr_get_output_property_reply_t *prop_reply = NULL;
	char *res = NULL;
	int cur;

	prop_reply = xcb_randr_get_output_property_reply(conn,
	    xcb_randr_get_output_property(conn, output, backlight_atom,
		XCB_ATOM_NONE, 0, 4, 0, 0),
	    &error);
	if (error != NULL || prop_reply == NULL) {
	    warnx("cannot get output backlight property");
	    goto cleanup_1;
	}
	if (prop_reply->type != XCB_ATOM_INTEGER ||
	    prop_reply->num_items != 1 ||
	    prop_reply->format != 32) {
		warnx("cannot not get current brightness");
		goto cleanup_2;
	}
	cur = *((int32_t *)
	    xcb_randr_get_output_property_data(prop_reply));

	snprintf(str, sizeof(str), "%d%%", cur * 100 / range);
	res = str;

cleanup_2:
	free(prop_reply);

cleanup_1:
	return res;
}

enum x_events { BRIGHTNESS_EVENT, AUDIO_EVENT };

void
x_event_loop(xcb_connection_t *conn, xcb_window_t root,
	int randr_event_base, int out)
{
	xcb_generic_event_t *evt;
	char brightness_event = (char)BRIGHTNESS_EVENT;
	char audio_event = (char)AUDIO_EVENT; 

	xcb_randr_select_input(conn, root,
	    XCB_RANDR_NOTIFY_MASK_OUTPUT_PROPERTY |
	    XCB_RANDR_NOTIFY_MASK_OUTPUT_CHANGE);

	xcb_grab_key(conn, 1, root, XCB_MOD_MASK_ANY, AUDIO_MUTE_KEYCODE,
	    XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
	xcb_grab_key(conn, 1, root, XCB_MOD_MASK_ANY, AUDIO_DOWN_KEYCODE,
	    XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
	xcb_grab_key(conn, 1, root, XCB_MOD_MASK_ANY, AUDIO_UP_KEYCODE,
	    XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);

	xcb_flush(conn);

	while ((evt = xcb_wait_for_event(conn)) != NULL) {
		if (evt->response_type == randr_event_base +
		    XCB_RANDR_NOTIFY_OUTPUT_CHANGE)
			write(out, &brightness_event, 1);
		else if (evt->response_type == XCB_KEY_RELEASE)
			write(out, &audio_event, 1);
		free(evt);
	}
}

void *
x_event_loop_thread_start(struct x_event_loop_args *args)
{
	x_event_loop(args->conn, args->root, args->event_base, args->out);

	return NULL;
}

/* Weather */

int weather_file() {
	int fd;

	fd = open(WEATHER_TIMESTAMP_FILENAME, O_RDONLY);
	if (fd < 0)
		warn("cannot open " WEATHER_TIMESTAMP_FILENAME);

	return fd;
}

char *
weather_info()
{
	static char str[WEATHER_BUFLEN], *strp, *ret;
	struct json_object *obj, *new_obj, *iter_obj;
	int i, len, buflen, n;

	strp = str;
	ret = NULL;
	buflen = WEATHER_BUFLEN;

	if ((obj = json_object_from_file(WEATHER_CURRENT_FILENAME))
	    == NULL) {
		warnx("could not load JSON file");
		goto cleanup_1;
	}

	if (!json_object_object_get_ex(obj, "main", &new_obj)) {
		warnx("could not find 'main'");
		goto cleanup_2;
	}
	if (!json_object_object_get_ex(new_obj, "temp", &new_obj)) {
		warnx("could not find 'main.temp'");
		goto cleanup_2;
	}
	n = snprintf(strp, buflen, "%.0f °C",
	    json_object_get_double(new_obj));

	if (!json_object_object_get_ex(obj, "weather", &new_obj)) {
		warnx("could not find 'weather'");
		goto cleanup_2;
	}
	if (!json_object_is_type(new_obj, json_type_array)) {
		warnx("'weather' is not an array");
		goto cleanup_2;
	}
	len = json_object_array_length(new_obj);
	for (i = 0; i < len; i++) {
		iter_obj = json_object_array_get_idx(new_obj, i);
		if (!json_object_is_type(iter_obj, json_type_object)) {
			warnx("weather[%d] is not an object", i);
			goto cleanup_2;
		}
		json_object_object_get_ex(iter_obj, "description",
		    &iter_obj);
		if (!json_object_is_type(iter_obj, json_type_string)) {
			warnx("weather[%d].description is not a string", i);
			goto cleanup_2;
		}
		strp += n;
		buflen -= n;
		n = snprintf(strp, buflen, ", %s",
		    json_object_get_string(iter_obj));
	}

	ret = str;

cleanup_2:
	json_object_put(obj);

cleanup_1:
	return ret;
}


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
	xcb_connection_t *display_connection;
	xcb_window_t root_window;
	xcb_atom_t backlight_atom;
	xcb_randr_output_t randr_output;
	struct kevent kev_in[EVENTS], kev[EVENTS];
	pthread_t x_event_loop_thread;
	struct x_event_loop_args bel_args;
	int mail_fd, kq, nev, i, brightness_range,
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

        if ((weather_fd = weather_file()) >= 0) {
                infos[INFO_WEATHER] = weather_info();
		EV_SET(&kev_in[n++], weather_fd, EVFILT_VNODE,
		    EV_ADD | EV_CLEAR, NOTE_WRITE, 0, NULL);
        }

        /* Brightness */

	if (brightness_init(&display_connection,
	    &root_window, &backlight_atom, &randr_output,
	    &randr_event_base, &brightness_range)) {

                infos[INFO_BRIGHTNESS] =
                    brightness_info(display_connection, randr_output,
                        backlight_atom, brightness_range);

		if (pipe(pipe_fd) == -1)
		        warn("could not open pipe");
		  
		EV_SET(&kev_in[n++], BRIGHTNESS_TIMER, EVFILT_TIMER,
		    EV_ADD, 0, BRIGHTNESS_INTERVAL, NULL);
		EV_SET(&kev_in[n++], pipe_fd[0], EVFILT_READ, EV_ADD, 0,
		    0, NULL);

		bel_args.conn = display_connection;
		bel_args.root = root_window;
		bel_args.event_base = randr_event_base;
		bel_args.out = pipe_fd[1];

		pthread_create(&x_event_loop_thread, NULL,
		    (void *(*)(void *))x_event_loop_thread_start,
		    &bel_args);
	}

        /* Audio */

	if (audio_init(&mixer_device, &mute_device)) {
                infos[INFO_AUDIO] = audio_info(mixer_device, mute_device);

		EV_SET(&kev_in[n++], AUDIO_TIMER, EVFILT_TIMER, EV_ADD,
		    0, AUDIO_INTERVAL, NULL);
		EV_SET(&kev_in[n++], pipe_fd[0], EVFILT_READ, EV_ADD, 0,
		    0, NULL);
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
					infos[INFO_BRIGHTNESS] =
					    brightness_info(
						display_connection,
						randr_output,
						backlight_atom,
						brightness_range);
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
						    brightness_info(
							display_connection,
							randr_output,
							backlight_atom,
							brightness_range);
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
