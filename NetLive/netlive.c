#include "netlive.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <pthread.h>
#include <SystemConfiguration/SystemConfiguration.h>

#pragma clang diagnostic ignored "-Wlogical-op-parentheses"

const char *remoteip = "8.8.8.8";
const char *remoteip6 = "2001:4860:4860::8844";
const char *remotedomain = "google.com";

pthread_t router, ipv4, ipv6, domain;
unsigned long seq;
uint16_t state;
char routerip[INET_ADDRSTRLEN];
char routerip6[INET6_ADDRSTRLEN];

bool work = false;

static u_short in_cksum(u_short *addr, int len) {
    int nleft;
    int sum;
    u_short *w;
    union {
        u_short us;
        u_char uc[2];
    } last;
    u_short answer;
    nleft = len;
    sum = 0;
    w = addr;
    while (nleft > 1) {
        sum += *w++;
        nleft -= 2;
    }
    if (nleft == 1) {
        last.uc[0] = *(u_char *)w;
        last.uc[1] = 0;
        sum += last.us;
    }
    sum = (sum >> 16) + (sum & 0xffff);
    sum += (sum >> 16);
    answer = ~sum;
    return answer;
}

static bool ip_icmp_ping(const char *dest) {
    bool reachability = false;
    struct icmp icmp;
    icmp.icmp_type = ICMP_ECHO;
    icmp.icmp_code = 0;
    icmp.icmp_cksum = 0;
    icmp.icmp_id = getpid();
    icmp.icmp_seq = htons(seq++);
    icmp.icmp_cksum = in_cksum((u_short *)&icmp, ICMP_MINLEN);
    struct sockaddr_in sockaddr;
    sockaddr.sin_family = AF_INET;
    sockaddr.sin_port = htons(0);
    sockaddr.sin_addr.s_addr = inet_addr(dest);
    socklen_t addrlen = sizeof(struct sockaddr_in);
    int s = socket(PF_INET, SOCK_DGRAM, IPPROTO_ICMP);
    struct timeval timeval;
    timeval.tv_sec = 3;
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &timeval, sizeof(struct timeval));
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &timeval, sizeof(struct timeval));
    if (sendto(s, &icmp, ICMP_MINLEN, 0, (const struct sockaddr *)&sockaddr, addrlen) > 0)
        if (recvfrom(s, &icmp, ICMP_MINLEN, 0, (struct sockaddr *)&sockaddr, &addrlen) > 0) {
            if (icmp.icmp_id == getpid())
            reachability = true;
        }
    close(s);
    return reachability;
}

static bool ip6_icmp_ping(const char *dest) {
    bool reachability = false;
    struct icmp icmp;
    icmp.icmp_type = ICMP_ECHO;
    icmp.icmp_code = 0;
    icmp.icmp_cksum = 0;
    icmp.icmp_id = getpid();
    icmp.icmp_seq = htons(seq++);
    icmp.icmp_cksum = in_cksum((u_short *)&icmp, ICMP_MINLEN);
    struct sockaddr_in6 sockaddr;
    sockaddr.sin6_family = AF_INET6;
    sockaddr.sin6_port = htons(0);
    struct in6_addr addr;
    inet_pton(AF_INET6, dest, &addr);
    sockaddr.sin6_addr = addr;
    socklen_t addrlen = sizeof(struct sockaddr_in6);
    int s = socket(PF_INET, SOCK_DGRAM, IPPROTO_ICMP);
    struct timeval timeval;
    timeval.tv_sec = 3;
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &timeval, sizeof(struct timeval));
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &timeval, sizeof(struct timeval));
    if (sendto(s, &icmp, ICMP_MINLEN, 0, (const struct sockaddr *)&sockaddr, addrlen) > 0)
        if (recvfrom(s, &icmp, ICMP_MINLEN, 0, (struct sockaddr *)&sockaddr, &addrlen) > 0)
            reachability = true;
    close(s);
    return reachability;
}

static void *dorouter(void *arg) {
    if (state & NETLIVE_IPV4_AVAILABLE && ip_icmp_ping(routerip) || state & NETLIVE_IPV6_AVAILABLE && ip6_icmp_ping(routerip))
        state |= NETLIVE_ROUTER_REACHABLE;
    return NULL;
}

static void *doipv4(void *arg) {
    if (state & NETLIVE_IPV4_AVAILABLE && ip_icmp_ping(remoteip))
        state |= NETLIVE_IPV4_REACHABLE;
    return NULL;
}

static void *doipv6(void *arg) {
    if (state & NETLIVE_IPV6_AVAILABLE && ip6_icmp_ping(remoteip))
        state |= NETLIVE_IPV6_REACHABLE;
    return NULL;
}

static void *dodomain(void *arg) {
    if (state & NETLIVE_IPV4_AVAILABLE && ip_icmp_ping(remotedomain) || state & NETLIVE_IPV6_AVAILABLE && ip6_icmp_ping(remotedomain))
        state |= NETLIVE_DOMAIN_REACHABLE;
    return NULL;
}

void netlive_once(void) {
    if (work) {
        netlive_handler(NETLIVE_WORKING | NETLIVE_DUMMY);
        return;
    }
    work = true;
    state = 0;
    CFStringRef str;
    CFDictionaryRef dict;
    SCDynamicStoreRef ds = SCDynamicStoreCreate(kCFAllocatorDefault, (CFStringRef)kCFNull, NULL, NULL);
    dict = SCDynamicStoreCopyValue(ds, CFSTR("State:/Network/Global/IPv4"));
    if (dict) {
        str = CFDictionaryGetValue(dict, CFSTR("Router"));
        if (str)
            strcpy(routerip, CFStringGetCStringPtr(str, kCFStringEncodingASCII));
        CFRelease(dict);
    }
    dict = SCDynamicStoreCopyValue(ds, CFSTR("State:/Network/Global/IPv6"));
    if (dict) {
        str = CFDictionaryGetValue(dict, CFSTR("Router"));
        if (str)
            strcpy(routerip6, CFStringGetCStringPtr(str, kCFStringEncodingASCII));
        CFRelease(dict);
    }
    CFRelease(ds);
    if (*routerip != '\0')
        state |= NETLIVE_IPV4_AVAILABLE;
    if (*routerip6 != '\0')
        state |= NETLIVE_IPV6_AVAILABLE;
    if (state)
        state |= NETLIVE_ROUTER_AVAILABLE;
    pthread_create(&router, NULL, dorouter, NULL);
    pthread_create(&ipv4, NULL, doipv4, NULL);
    pthread_create(&ipv6, NULL, doipv6, NULL);
    pthread_create(&domain, NULL, dodomain, NULL);
    pthread_join(router, NULL);
    pthread_join(ipv4, NULL);
    pthread_join(ipv6, NULL);
    pthread_join(domain, NULL);
    work = false;
    netlive_handler(state);
}

void netlive_cancel(void) {
    if (!work)
        netlive_handler(NETLIVE_WAITING | NETLIVE_DUMMY);
    else {
        netlive_handler(state | NETLIVE_TIMEOUT);
        pthread_cancel(router);
        pthread_cancel(ipv4);
        pthread_cancel(ipv6);
        pthread_cancel(domain);
        work = false;
    }
}
