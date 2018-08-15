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

#define IDLESTR "idle player\n"
#define TITLESTR "\nTitle: "
#define NAMESTR "\nName: "
#define CURRENTSTR "currentsong\n"

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

        if (numbytes <= 7 || strncmp(buf, "OK MPD ", 7) != 0) {
                fprintf(stderr, "Not an MPD server\n");
                return -1;
        }

        send(sockfd, IDLESTR, (sizeof IDLESTR) - 1, 0);

        return sockfd;
}

char *
mpd_info(int sockfd)
{
        int numbytes;
        char *name, *title;
        char buf[MAXDATASIZE];
        static char info[MPD_INFOLEN];

        buf[0] = '\n';

        if ((numbytes = recv(sockfd, buf + 1, MAXDATASIZE - 2, 0))
            == -1) {
                perror("recv");
                exit(1);
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

        snprintf(info, MPD_INFOLEN, "%s: %s\n",
            name ? name : "UNKNOWN NAME", title ? title : "UNKNOWN TITLE");

        send(sockfd, IDLESTR, (sizeof IDLESTR) - 1, 0);

        return info;
}

/*
int
main(int argc, char *argv[])
{
        int sockfd;
        char buf[MAXDATASIZE];

        sockfd = mpd_init();

        buf[0] = '\n';
        for (;;) mpd_str(sockfd, buf);

        close(sockfd);

        return 0;
}
*/
