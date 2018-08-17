#define MPD_INFOLEN 128

int     mpd_init();
void    mpd_idle_start(int);
void    mpd_idle_end(int);
char   *mpd_info(int);
