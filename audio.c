#include <sys/types.h>
#include <sys/audioio.h>
#include <sys/ioctl.h>
#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define MIXER_DEV_PATH "/dev/mixer"
#define MIXER_DEVICE_CLASS "outputs"
#define MIXER_DEVICE "master"
#define MIXER_MUTE_DEVICE "mute"

#define AUDIO_BUFLEN 8

static int mixer_device, mute_device, initialized = 0;

static int audio_print_volume(char *, size_t, int);

int
audio_init()
{
        if (initialized)
                errx(1, "audio_init called twice");

        initialized = 1;

	struct mixer_devinfo devinfo;
	int fd, class_index, ret;

	ret = 0;
	class_index = mixer_device = mute_device = -1;

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
			mixer_device = devinfo.index;
			break;
		}
	}
	if (mixer_device == -1) {
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
			mute_device = devinfo.index;
			break;
		}
	}
	if (mute_device == -1) {
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
audio_info()
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

