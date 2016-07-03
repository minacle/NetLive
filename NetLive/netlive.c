#include "netlive.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <pthread.h>
#include <sys/time.h>
#include <time.h>
#include <SystemConfiguration/SystemConfiguration.h>

#pragma clang diagnostic ignored "-Wlogical-op-parentheses"

const char *remoteip = "8.8.8.8";
const char *remoteip6 = "2001:4860:4860::8844";
const char *remotedomain = "google.com";

pthread_attr_t attr;
pthread_t listen_thread;
pthread_t router_thread, ipv4_thread, ipv6_thread, domain_thread;

struct timeval router_timeval, ipv4_timeval, ipv6_timeval, domain_timeval;

unsigned long seq = -4;
netlive_result_t result;

char routerip[INET_ADDRSTRLEN];
char routerip6[INET6_ADDRSTRLEN];

bool work;
unsigned left;

static uint16_t in_cksum(uint16_t *buf, size_t len) {
    size_t nleft;
    int32_t sum;
    const uint16_t *w;
    union {
        uint16_t us;
        uint8_t uc[2];
    } last;
    uint16_t answer;
    nleft = len;
    sum = 0;
    w = buf;
    while (nleft > 1) {
        sum += *w++;
        nleft -= 2;
    }
    if (nleft == 1) {
        last.uc[0] = *(uint8_t *)w;
        last.uc[1] = 0;
        sum += last.us;
    }
    sum = (sum >> 16) + (sum & 0xffff);
    sum += (sum >> 16);
    answer = ~sum;
    return answer;
}

static void *dolisten(void *arg) {
    char buf[64];
    struct sockaddr_storage sockaddr;
    socklen_t addrlen = sizeof(struct sockaddr_storage);
    int s = socket(PF_INET, SOCK_DGRAM, IPPROTO_ICMP);
    struct timeval timeval;
    timeval.tv_sec = 3;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &timeval, sizeof(struct timeval));
    while (left--) {
        ssize_t len = recvfrom(s, buf, sizeof(buf), 0, (struct sockaddr *)&sockaddr, &addrlen);
        struct timeval timeval;
        gettimeofday(&timeval, NULL);
        if (len > 0) {
            struct ip ip = *(struct ip *)buf;
            struct icmp *icmp = (struct icmp *)(buf + (ip.ip_hl << 2));
            unsigned short cksum = icmp->icmp_cksum;
            icmp->icmp_cksum = 0;
            if (cksum != in_cksum((uint16_t *)icmp, ICMP_MINLEN) || icmp->icmp_id != getpid())
                continue;
            switch (ntohs(icmp->icmp_seq) - seq) {
                case 0:
                    result.time.router = (timeval.tv_sec - router_timeval.tv_sec) * 1000000 + timeval.tv_usec - router_timeval.tv_usec;
                    result.state |= NETLIVE_ROUTER_REACHABLE;
                    break;
                case 1:
                    result.time.ipv4 = (timeval.tv_sec - ipv4_timeval.tv_sec) * 1000000 + timeval.tv_usec - ipv4_timeval.tv_usec;
                    result.state |= NETLIVE_IPV4_REACHABLE;
                    break;
                case 2:
                    result.time.ipv6 = (timeval.tv_sec - ipv6_timeval.tv_sec) * 1000000 + timeval.tv_usec - ipv6_timeval.tv_usec;
                    result.state |= NETLIVE_IPV6_REACHABLE;
                    break;
                case 3:
                    result.time.domain = (timeval.tv_sec - domain_timeval.tv_sec) * 1000000 + timeval.tv_usec - domain_timeval.tv_usec;
                    result.state |= NETLIVE_DOMAIN_REACHABLE;
                    break;
                default:
                    continue;
            }
        }
    }
    close(s);
    if (work)
        netlive_handler(result);
    return NULL;
}

static void *dorouter(void *arg) {
    struct icmp icmp;
    if (result.state & NETLIVE_IPV4_AVAILABLE) {
        struct icmp icmp;
        icmp.icmp_type = ICMP_ECHO;
        icmp.icmp_code = 0;
        icmp.icmp_cksum = 0;
        icmp.icmp_id = getpid();
        icmp.icmp_seq = htons(seq);
        icmp.icmp_cksum = in_cksum((uint16_t *)&icmp, ICMP_MINLEN);
        struct sockaddr_in sockaddr;
        sockaddr.sin_family = AF_INET;
        sockaddr.sin_port = htons(0);
        sockaddr.sin_addr.s_addr = inet_addr(routerip);
        socklen_t addrlen = sizeof(struct sockaddr_in);
        int s = socket(PF_INET, SOCK_DGRAM, IPPROTO_ICMP);
        struct timeval timeval;
        timeval.tv_sec = 3;
        setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &timeval, sizeof(struct timeval));
        gettimeofday(&router_timeval, NULL);
        sendto(s, &icmp, ICMP_MINLEN, 0, (const struct sockaddr *)&sockaddr, addrlen);
        close(s);
    }
    else if (result.state & NETLIVE_IPV6_AVAILABLE) {
        icmp.icmp_type = ICMP_ECHO;
        icmp.icmp_code = 0;
        icmp.icmp_cksum = 0;
        icmp.icmp_id = getpid();
        icmp.icmp_seq = htons(seq);
        icmp.icmp_cksum = in_cksum((uint16_t *)&icmp, ICMP_MINLEN);
        struct sockaddr_in6 sockaddr;
        sockaddr.sin6_family = AF_INET6;
        sockaddr.sin6_port = htons(0);
        struct in6_addr addr;
        inet_pton(AF_INET6, routerip6, &addr);
        sockaddr.sin6_addr = addr;
        socklen_t addrlen = sizeof(struct sockaddr_in6);
        int s = socket(PF_INET, SOCK_DGRAM, IPPROTO_ICMPV6);
        struct timeval timeval;
        timeval.tv_sec = 3;
        setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &timeval, sizeof(struct timeval));
        gettimeofday(&router_timeval, NULL);
        sendto(s, &icmp, ICMP_MINLEN, 0, (const struct sockaddr *)&sockaddr, addrlen);
        close(s);
    }
    return NULL;
}

static void *doipv4(void *arg) {
    if (result.state & NETLIVE_IPV4_AVAILABLE) {
        struct icmp icmp;
        icmp.icmp_type = ICMP_ECHO;
        icmp.icmp_code = 0;
        icmp.icmp_cksum = 0;
        icmp.icmp_id = getpid();
        icmp.icmp_seq = htons(seq + 1);
        icmp.icmp_cksum = in_cksum((uint16_t *)&icmp, ICMP_MINLEN);
        struct sockaddr_in sockaddr;
        sockaddr.sin_family = AF_INET;
        sockaddr.sin_port = htons(0);
        sockaddr.sin_addr.s_addr = inet_addr(remoteip);
        socklen_t addrlen = sizeof(struct sockaddr_in);
        int s = socket(PF_INET, SOCK_DGRAM, IPPROTO_ICMP);
        struct timeval timeval;
        timeval.tv_sec = 3;
        setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &timeval, sizeof(struct timeval));
        gettimeofday(&ipv4_timeval, NULL);
        sendto(s, &icmp, ICMP_MINLEN, 0, (const struct sockaddr *)&sockaddr, addrlen);
        close(s);
    }
    return NULL;
}

static void *doipv6(void *arg) {
    if (result.state & NETLIVE_IPV6_AVAILABLE) {
        struct icmp icmp;
        icmp.icmp_type = ICMP_ECHO;
        icmp.icmp_code = 0;
        icmp.icmp_cksum = 0;
        icmp.icmp_id = getpid();
        icmp.icmp_seq = htons(seq + 2);
        icmp.icmp_cksum = in_cksum((uint16_t *)&icmp, ICMP_MINLEN);
        struct sockaddr_in6 sockaddr;
        sockaddr.sin6_family = AF_INET6;
        sockaddr.sin6_port = htons(0);
        struct in6_addr addr;
        inet_pton(AF_INET6, remoteip6, &addr);
        sockaddr.sin6_addr = addr;
        socklen_t addrlen = sizeof(struct sockaddr_in6);
        int s = socket(PF_INET, SOCK_DGRAM, IPPROTO_ICMPV6);
        struct timeval timeval;
        timeval.tv_sec = 3;
        setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &timeval, sizeof(struct timeval));
        gettimeofday(&ipv6_timeval, NULL);
        sendto(s, &icmp, ICMP_MINLEN, 0, (const struct sockaddr *)&sockaddr, addrlen);
        close(s);
    }
    return NULL;
}

static void *dodomain(void *arg) {
    struct icmp icmp;
    if (result.state & NETLIVE_IPV4_AVAILABLE) {
        struct icmp icmp;
        icmp.icmp_type = ICMP_ECHO;
        icmp.icmp_code = 0;
        icmp.icmp_cksum = 0;
        icmp.icmp_id = getpid();
        icmp.icmp_seq = htons(seq + 3);
        icmp.icmp_cksum = in_cksum((uint16_t *)&icmp, ICMP_MINLEN);
        struct sockaddr_in sockaddr;
        sockaddr.sin_family = AF_INET;
        sockaddr.sin_port = htons(0);
        sockaddr.sin_addr.s_addr = inet_addr(remotedomain);
        socklen_t addrlen = sizeof(struct sockaddr_in);
        int s = socket(PF_INET, SOCK_DGRAM, IPPROTO_ICMP);
        struct timeval timeval;
        timeval.tv_sec = 3;
        setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &timeval, sizeof(struct timeval));
        gettimeofday(&domain_timeval, NULL);
        sendto(s, &icmp, ICMP_MINLEN, 0, (const struct sockaddr *)&sockaddr, addrlen);
        close(s);
    }
    else if (result.state & NETLIVE_IPV6_AVAILABLE) {
        icmp.icmp_type = ICMP_ECHO;
        icmp.icmp_code = 0;
        icmp.icmp_cksum = 0;
        icmp.icmp_id = getpid();
        icmp.icmp_seq = htons(seq + 3);
        icmp.icmp_cksum = in_cksum((uint16_t *)&icmp, ICMP_MINLEN);
        struct sockaddr_in6 sockaddr;
        sockaddr.sin6_family = AF_INET6;
        sockaddr.sin6_port = htons(0);
        struct in6_addr addr;
        inet_pton(AF_INET6, remotedomain, &addr);
        sockaddr.sin6_addr = addr;
        socklen_t addrlen = sizeof(struct sockaddr_in6);
        int s = socket(PF_INET, SOCK_DGRAM, IPPROTO_ICMPV6);
        struct timeval timeval;
        timeval.tv_sec = 3;
        setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &timeval, sizeof(struct timeval));
        gettimeofday(&domain_timeval, NULL);
        sendto(s, &icmp, ICMP_MINLEN, 0, (const struct sockaddr *)&sockaddr, addrlen);
        close(s);
    }
    return NULL;
}

void netlive_once(void) {
    if (work)
        return;
    work = true;
    memset(&result, 0, sizeof(netlive_result_t));
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
    left = 0;
    if (*routerip != '\0') {
        result.state |= NETLIVE_IPV4_AVAILABLE;
        left++;
    }
    if (*routerip6 != '\0') {
        result.state |= NETLIVE_IPV6_AVAILABLE;
        left++;
    }
    if (result.state) {
        result.state |= NETLIVE_ROUTER_AVAILABLE;
        left++;
    }
    if (left > 0)
        left++;
    seq += 4;
    pthread_create(&listen_thread, NULL, dolisten, NULL);
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_create(&router_thread, &attr, dorouter, NULL);
    pthread_create(&ipv4_thread, &attr, doipv4, NULL);
    pthread_create(&ipv6_thread, &attr, doipv6, NULL);
    pthread_create(&domain_thread, &attr, dodomain, NULL);
    pthread_attr_destroy(&attr);
    pthread_join(listen_thread, NULL);
    work = false;
    netlive_handler(result);
}

void netlive_cancel(void) {
    if (!work)
        return;
    pthread_cancel(listen_thread);
    result.state |= NETLIVE_TIMEOUT;
    netlive_handler(result);
    work = false;
}
