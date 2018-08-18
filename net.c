#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <netinet/if_ether.h>
#include <net/if_trunk.h>
#include <netinet/in.h>
#include <err.h>
#include <string.h>
#include <unistd.h>

#define IFNAME "trunk0"

char *
net_info()
{
	static char str[IFNAMSIZ + 1 +
	    ((INET6_ADDRSTRLEN) > (INET_ADDRSTRLEN) ?
	     (INET6_ADDRSTRLEN) : (INET_ADDRSTRLEN))];
	struct ifreq ifr;
	struct trunk_reqall ra;
	struct trunk_reqport *rp, rpbuf[TRUNK_MAX_PORTS];
	char *res = NULL;
	void *addrp;
	int i, s, len;

	rp = NULL;

	if ((s = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
	    warn("coud not open socket");
	    return res;
	}

	/* Trunk ports */

	strlcpy(ra.ra_ifname, IFNAME, sizeof(ra.ra_ifname));
	ra.ra_size = sizeof(rpbuf);
	ra.ra_port = rpbuf;

	if (ioctl(s, SIOCGTRUNK, &ra)) {
		warn("could not query trunk properties");
		goto cleanup;
	}

	if (!(ra.ra_proto & TRUNK_PROTO_FAILOVER)) {
		warnx("trunk protocol is not 'failover'");
		goto cleanup;
	}

	for (i = 0; i < ra.ra_ports; i++)
		if (rpbuf[i].rp_flags & TRUNK_PORT_ACTIVE) {
			rp = &rpbuf[i];
			break;
		}

	if (rp == NULL) {
		warnx("no active trunk port found");
		goto cleanup;
	}


	/* IP address */

	strlcpy(ifr.ifr_name, IFNAME, sizeof(ifr.ifr_name));
	if (ioctl(s, SIOCGIFADDR, &ifr) == -1) {
		warn("could not query inet address");
		goto cleanup;
	}

	switch (ifr.ifr_addr.sa_family) {
	case AF_INET:
		addrp = &((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr;
		break;
	case AF_INET6:
		addrp = &((struct sockaddr_in6 *)&ifr.ifr_addr)->sin6_addr;
		break;
	default:
		warnx("unknown inet address protocol");
		goto cleanup;
	}

	
	/* Output */

	strlcpy(str, rp->rp_portname, sizeof(str));

	len = strnlen(rp->rp_portname, IFNAMSIZ);
	str[len] = ' ';

	if (inet_ntop(ifr.ifr_addr.sa_family, addrp,
	    str + len + 1, sizeof(str) - len - 1) == NULL) {
		warn("could not convert inet address");
		goto cleanup;
	}

	res = str;

cleanup:
	close(s);
	return res;
}
