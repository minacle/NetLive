#include <errno.h>
#include <signal.h>
#include <stdint.h>

#pragma once

#define NETLIVE_AVAILABLE 1
#define NETLIVE_REACHABLE 2

#define NETLIVE_ROUTER 0
#define NETLIVE_IPV4 2
#define NETLIVE_IPV6 4
#define NETLIVE_DOMAIN 6

#define NETLIVE_ROUTER_AVAILABLE (NETLIVE_AVAILABLE << NETLIVE_ROUTER)
#define NETLIVE_ROUTER_REACHABLE (NETLIVE_REACHABLE << NETLIVE_ROUTER)
#define NETLIVE_IPV4_AVAILABLE (NETLIVE_AVAILABLE << NETLIVE_IPV4)
#define NETLIVE_IPV4_REACHABLE (NETLIVE_REACHABLE << NETLIVE_IPV4)
#define NETLIVE_IPV6_AVAILABLE (NETLIVE_AVAILABLE << NETLIVE_IPV6)
#define NETLIVE_IPV6_REACHABLE (NETLIVE_REACHABLE << NETLIVE_IPV6)
#define NETLIVE_DOMAIN_AVAILABLE (NETLIVE_AVAILABLE << NETLIVE_DOMAIN)
#define NETLIVE_DOMAIN_REACHABLE (NETLIVE_REACHABLE << NETLIVE_DOMAIN)

#define NETLIVE_MESSAGE 8

#define NETLIVE_TIMEOUT (1 << NETLIVE_MESSAGE)
#define NETLIVE_WORKING (2 << NETLIVE_MESSAGE)
#define NETLIVE_WAITING (3 << NETLIVE_MESSAGE)

#define NETLIVE_DUMMY NETLIVE_DOMAIN_AVAILABLE

typedef struct netlive_result netlive_result_t;

struct netlive_result {
    unsigned short state;
    struct time {
        unsigned long router;
        unsigned long ipv4;
        unsigned long ipv6;
        unsigned long domain;
    } time;
};

void netlive_once(void);
void netlive_cancel(void);
void netlive_handler(netlive_result_t);
