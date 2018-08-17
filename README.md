# lemonbar-status

## Description

`lemonbar-status` outputs the system status on standard output.

The program does not take any command line arguments and does not
use a configuration file. I do not plan to make this program
generally useable. But you are invited to take its source code
and adapt it to your own needs.

The information displayed includes

* the current title played by the Music Player Daemon,
* the mail status,
* the active network interface name and the IP address,
* the battery status,
* the display brightness,
* the audio volume,
* the current weather and
* the current date and time.
* MPD status and current song

The display is updated periodically with an interval of 10
seconds. If possible, events generated by the system are
intercepted and the information is updated immediately.

## Prerequisites

### Compilation

To compile the program you presumably must be running OpenBSD
(tested on version 6.2), you have a recent X server installation
and the json-c library is installed.

### Runtime

The following conditions must be met for running the
`lemonbar-status`.

* You are running OpenBSD.
* Your username is `wilfried`.
* You are running the `mpd` music player daemon.
* Your are using `trunk0` as your network connection
* Your audio system has an `outputs.master` and an
  `outputs.master.mute` mixer device.
* My `weather` script is installed anywhere in `$PATH` and
  executable. It must output its data to `$HOME/.config/weather/`.

If one of these conditions is not met, the program will either
not start up or the corresponding output will be disabled.

## Program Structure

If you want to change this program, then this section will give
you some rough overview about the structure.

The main() function initializes the information sources and
contains a kqueue event loop for processing events. It also
starts a thread which processes X events. These events are
coupled to the kqueue loop by sending the signals USR1 and USR2
to the process. In this way no mutexes or other means of thread
synchronization is needed, as the kqueue already provides it.

Each information source is represented by a set of functions.
Usually there is initialization function, e.g. `mail_init()`, which
reserves and prepares the resources needed for reacting on the
events in the kqueue loop. Then there are the "info" functions,
e.g. `mail_info()`, which return a string or NULL if the
information cannot be displayed.

The enumeration `infos` determines the output sequence and also
the size of the `infos` array in `main()` where the strings
returned by the `*_info()` functions are stored.

## Remarks

The program grabs the XF86AudioMute, XF86AudioLowerVolume and XF86AudioRaiseVolume keys. Therefore applications will not receive those keys. This is my personal preference. But it can be changed in the X event loop with the `xcb_allow_events()` function.
