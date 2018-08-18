#include <fcntl.h>
#include <err.h>
#include <stdlib.h>
#include <stdio.h>
#include <json-c/json.h>

#define WEATHER_CURRENT_FILENAME "/home/wilfried/.cache/weather/current"
#define WEATHER_TIMESTAMP_FILENAME "/home/wilfried/.cache/weather/timestamp"
#define WEATHER_BUFLEN 48

int weather_init() {
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
