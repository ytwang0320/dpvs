/*
 * DPVS is a software load balancer (Virtual Server) based on DPDK.
 *
 * Copyright (C) 2017 iQIYI (www.iqiyi.com).
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#ifndef __DPVS_ROUTE_CONF_H__
#define __DPVS_ROUTE_CONF_H__

#include <arpa/inet.h>
#include <net/if.h>

enum {
    /* get */
    SOCKOPT_GET_NEIGH_SHOW = 600,

    /* set */
    SOCKOPT_SET_NEIGH_ADD,
    SOCKOPT_SET_NEIGH_DEL,
};

struct dp_vs_neigh_conf {
    int af;
    uint8_t flag;
    union inet_addr ip_addr;
    struct ether_addr eth_addr;
    uint32_t que_num;
    char ifname[IFNAMSIZ];
}__attribute__((__packed__));

struct dp_vs_neigh_conf_array {
    int  n_neigh;
    struct dp_vs_neigh_conf addrs[0];
}__attribute__((__packed__));

#endif 
