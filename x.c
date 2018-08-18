#include <xcb/xcb.h>
#include <xcb/randr.h>
#include <err.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "x.h"

#define BRIGHTNESS_BUFLEN 5
#define OUTPUT_NAME "eDP1"
#define AUDIO_MUTE_KEYCODE 160
#define AUDIO_DOWN_KEYCODE 174
#define AUDIO_UP_KEYCODE 176

struct x_event_loop_args
{
	xcb_connection_t *conn;
	xcb_window_t root;
	int event_base;
	int out;
};

static void   *x_event_loop_thread_start(struct x_event_loop_args *);

static xcb_connection_t *display_connection;
static xcb_window_t root_window;
static xcb_atom_t backlight_atom_out;
static xcb_randr_output_t output_out;
static int range_out, randr_event_base, initialized = 0;

static pthread_t x_event_loop_thread;
static struct x_event_loop_args bel_args;

/*
	xcb_connection_t *display_connection;
	xcb_window_t root_window;
	xcb_atom_t backlight_atom;
	xcb_randr_output_t randr_output;
        int brightness_range;

                if (brightness_init(pipe_fd[1], &display_connection,
                    &root_window, &backlight_atom, &randr_output,
                    &randr_event_base, &brightness_range)) {

                        infos[INFO_BRIGHTNESS] =
                            brightness_info(display_connection,
                                randr_output, backlight_atom,
                                brightness_range);
*/

int
x_init(int pipe_fd)
{
        if (initialized)
                errx(1, "brightness_init called twice");

        initialized = 1;

	xcb_generic_error_t *error = NULL;
	xcb_connection_t *conn = NULL;
	const xcb_query_extension_reply_t *randr_data;
	xcb_randr_query_version_reply_t *ver_reply = NULL;
	xcb_intern_atom_reply_t *backlight_reply = NULL;
	xcb_atom_t backlight_atom;
	xcb_screen_t *screen = NULL;
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
	display_connection = conn;

	randr_data = xcb_get_extension_data(conn, &xcb_randr_id);
	if (!randr_data->present) {
		warnx("cannot find RandR extension");
		goto cleanup_2;
	}

        randr_event_base = randr_data->first_event;

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
	backlight_atom_out = backlight_atom;

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
	root_window = screen->root;

	resources_reply = xcb_randr_get_screen_resources_reply(conn,
	    xcb_randr_get_screen_resources(conn, root_window), &error);
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
	output_out = outputs[i];

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
	range_out = limits[1] - limits[0];

	res = 1;

        bel_args.conn = display_connection;
        bel_args.root = root_window;
        bel_args.event_base = randr_event_base;
        bel_args.out = pipe_fd;

        pthread_create(&x_event_loop_thread, NULL,
            (void *(*)(void *))x_event_loop_thread_start,
            &bel_args);

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
x_info()
{
    	static char str[BRIGHTNESS_BUFLEN];
	xcb_generic_error_t *error = NULL;
	xcb_randr_get_output_property_reply_t *prop_reply = NULL;
	char *res = NULL;
	int cur;

	prop_reply = xcb_randr_get_output_property_reply(display_connection,
	    xcb_randr_get_output_property(display_connection, output_out,
                backlight_atom_out, XCB_ATOM_NONE, 0, 4, 0, 0),
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

	snprintf(str, sizeof(str), "%d%%", cur * 100 / range_out);
	res = str;

cleanup_2:
	free(prop_reply);

cleanup_1:
	return res;
}

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
		    XCB_RANDR_NOTIFY_OUTPUT_CHANGE) {
			write(out, &brightness_event, 1);
                }
		else if (evt->response_type == XCB_KEY_RELEASE) {
			write(out, &audio_event, 1);
                }
		free(evt);
	}
}

void *
x_event_loop_thread_start(struct x_event_loop_args *args)
{
	x_event_loop(args->conn, args->root, args->event_base, args->out);

	return NULL;
}
