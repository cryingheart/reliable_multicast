// Copyright (C) 2018, Jaguar Land Rover
// This program is licensed under the terms and conditions of the
// Mozilla Public License, version 2.0.  The full text of the
// Mozilla Public License is at https://www.mozilla.org/MPL/2.0/
//
// Author: Magnus Feuer (mfeuer1@jaguarlandrover.com)



#define _GNU_SOURCE
#include "reliable_multicast.h"
#include <errno.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <memory.h>


int rmc_sub_timeout_process(rmc_sub_context_t* ctx)
{
    usec_timestamp_t ts = rmc_usec_monotonic_timestamp();
    pub_sub_list_t subs;
    pub_subscriber_t* sub = 0;
    int res = 0;
    sub_packet_t* pack = 0;
    
    if (!ctx)
        return EINVAL;


    while((pack = sub_get_next_acknowledge_ready(&ctx->sub_ctx))) {
        sub_packet_acknowledged(pack);
        _rmc_sub_write_acknowledgement(ctx, pack);
    }
    return 0;
}


int rmc_sub_timeout_get_next(rmc_sub_context_t* ctx, usec_timestamp_t* result)
{
    usec_timestamp_t oldest_received_ts = 0;
    usec_timestamp_t current_ts = rmc_usec_monotonic_timestamp();

    if (!ctx || !result)
        return EINVAL;
    

    // Get the timestamp when the oldest packet was received that we haven
    // yet to send an acknowledgement back to the publisher for.
    sub_get_oldest_unacknowledged_packet(&ctx->sub_ctx, &oldest_received_ts);
    
    // Did we have infinite timeout?
    if (oldest_received_ts == -1) {
        *result = -1;
        return 0;
    }
        

    oldest_received_ts = oldest_received_ts + ctx->ack_timeout - current_ts;
    if (oldest_received_ts < 0)
        oldest_received_ts = 0;


    *result = oldest_received_ts;
    return 0;
}
