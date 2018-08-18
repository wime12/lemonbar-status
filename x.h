#define BRIGHTNESS_INTERVAL (10 * 1000)

enum x_events { BRIGHTNESS_EVENT, AUDIO_EVENT };

int     x_init(int);
char   *x_info();
