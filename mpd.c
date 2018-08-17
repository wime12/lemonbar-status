#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <arpa/inet.h>

#include "mpd.h"

#define PORT "6600"

#define MAXDATASIZE 1024 /* TODO: What is sensible for mpd? */

#define OKSTR "OK MPD "
#define IDLESTR "idle player\n"
#define CURRENTSTR "currentsong\n"
#define TITLESTR "\nTitle: "
#define NAMESTR "\nName: "
#define STATUSSTR "status\n"
#define STATESTR "\nstate: "

char *
find_tag(char *str, char* tag, size_t offset)
{
        char *pos;

        pos = strstr(str, tag);

        return pos ? pos + offset : NULL;
}

void
terminate_str(char *str)
{
        char *pos = strchr(str, '\n');
        *pos = '\0';
}

void *
get_in_addr(struct sockaddr *sa)
{
        if (sa->sa_family == AF_INET) {
                return &(((struct sockaddr_in*)sa)->sin_addr);
        }

        return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int
mpd_init()
{
        int sockfd, numbytes;
        char buf[64];
        struct addrinfo hints, *servinfo, *p;
        int rv;
        char s[INET6_ADDRSTRLEN];

        memset(&hints, 0, sizeof hints);
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;

        if ((rv = getaddrinfo("localhost", PORT, &hints, &servinfo)) != 0) {
                fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
                return -1;
        }

        for (p = servinfo; p != NULL; p = p->ai_next) {
                if ((sockfd = socket(p->ai_family, p->ai_socktype,
                    p->ai_protocol)) == -1) {
                        perror("client: socket");
                        continue;
                }

                if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
                        close(sockfd);
                        perror("client: connect");
                        continue;
                }

                break;
        }

        if (p == NULL) {
                fprintf(stderr, "client: failed to connect\n");
                return -1;
        }

        inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr),
            s, sizeof s);

        freeaddrinfo(servinfo);

        if ((numbytes = recv(sockfd, buf, MAXDATASIZE - 1, 0)) == -1) {
                perror("recv");
                return -1;
        }

        buf[numbytes] = '\0';

        if (numbytes <= sizeof OKSTR  - 1 ||
            strncmp(buf, OKSTR, sizeof OKSTR - 1) != 0) {
                fprintf(stderr, "Not an MPD server\n");
                return -1;
        }

        return sockfd;
}

void
mpd_idle_start(int sockfd)
{
        send(sockfd, IDLESTR, (sizeof IDLESTR) - 1, 0);
}

void
mpd_idle_end(int sockfd)
{
        int numbytes;
        char *buf[MAXDATASIZE];

        if ((numbytes = recv(sockfd, buf + 1, MAXDATASIZE - 2, 0))
            == -1) {
                perror("recv");
                exit(1);
        }
}

char *
mpd_info(int sockfd)
{
        int numbytes, nprinted;
        char *name, *title, *status;
        char buf[MAXDATASIZE];
        static char info[MPD_INFOLEN];

        buf[0] = '\n';
        nprinted = 0;

        send(sockfd, STATUSSTR, (sizeof STATUSSTR) - 1, 0);
        if ((numbytes = recv(sockfd, buf + 1, MAXDATASIZE - 2, 0))
            == -1) {
                perror("recv");
                exit(1);
        }

        buf[numbytes + 1] = '\0';
        status = find_tag(buf, STATESTR, sizeof STATESTR - 1);
        if (status) {
                terminate_str(status);
                if (strcmp(status, "stop") == 0)
                        nprinted = snprintf(info, MPD_INFOLEN,
                                        "STOPPED - ");
                else if (strcmp(status, "pause") == 0)
                        nprinted = snprintf(info, MPD_INFOLEN, "PAUSED - ");
        }

        send(sockfd, CURRENTSTR, (sizeof CURRENTSTR) - 1, 0);
        if ((numbytes = recv(sockfd, buf + 1, MAXDATASIZE - 2, 0))
            == -1) {
                perror("recv");
                exit(1);
        }

        buf[numbytes + 1] = '\0';

        name = find_tag(buf, NAMESTR, sizeof NAMESTR - 1);
        if (name) terminate_str(name);

        title = find_tag(buf, TITLESTR, sizeof TITLESTR - 1);
        if (title) terminate_str(title);

        snprintf(info + nprinted, MPD_INFOLEN - nprinted, "%s: %s",
            name ? name : "UNKNOWN NAME", title ? title : "UNKNOWN TITLE");

        return info;
}
