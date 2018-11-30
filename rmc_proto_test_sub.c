// Copyright (C) 2018, Jaguar Land Rover
// This program is licensed under the terms and conditions of the
// Mozilla Public License, version 2.0.  The full text of the
// Mozilla Public License is at https://www.mozilla.org/MPL/2.0/
//
// Author: Magnus Feuer (mfeuer1@jaguarlandrover.com)

#include "rmc_proto_test_common.h"

// Indexed by publisher node_id, as received in the
// payload 
typedef struct {
    enum {
        // We will not process traffic for this node_id.
        // Any traffic received will trigger error.
        RMC_TEST_SUB_INACTIVE = 0,  

        // We expect traffic on this ctx-id (as provided by -e <ctx-id>,
        // But haven't seen any yet.
        RMC_TEST_SUB_NOT_STARTED = 1, 

        // We are in the process of receiving traffic 
        RMC_TEST_SUB_IN_PROGRESS = 2,

        // We have received all expected traffic for the given ctx-id.
        RMC_TEST_SUB_COMPLETED = 3
    } status;    

    uint64_t max_expected;
    uint64_t max_received;
} sub_expect_t;


static uint8_t _test_print_pending(sub_packet_node_t* node, void* dt)
{
    sub_packet_t* pack = (sub_packet_t*) node->data;
    int indent = (int) (uint64_t) dt;

    printf("%*cPacket          %p\n", indent*2, ' ', pack);
    printf("%*c  PID             %lu\n", indent*2, ' ', pack->pid);
    printf("%*c  Payload Length  %d\n", indent*2, ' ', pack->payload_len);
    putchar('\n');
    return 1;
}


static int _descriptor(rmc_sub_context_t* ctx,
                       rmc_index_t index)
{
    switch(index) {
    case RMC_MULTICAST_INDEX:
        return ctx->mcast_recv_descriptor;

    default:
        return ctx->conn_vec.connections[index].descriptor;

    }
}

static int check_exit_condition( sub_expect_t* expect, int expect_sz)
{
    int ind = expect_sz;

    while(ind--) {
        if (expect[ind].status == RMC_TEST_SUB_NOT_STARTED ||
            expect[ind].status == RMC_TEST_SUB_IN_PROGRESS)
            return 0;
    }
    return 1;
}

static int process_incoming_data(rmc_sub_context_t* ctx, sub_packet_t* pack, sub_expect_t* expect, int expect_sz)
{
    rmc_context_id_t node_id = 0;
    uint64_t max_expected = 0;
    uint64_t current = 0;
    
    _test("rmc_proto_test_sub[%d.%d] process_incoming_data(): %s", 3, 1, pack?0:ENODATA);

    if (sscanf(pack->payload, "%u:%lu:%lu", &node_id, &current, &max_expected) != 3) {
        printf("rmc_proto_test_sub(): Payload [%s] could not be scanned by [%%u:%%lu:%%lu]\n",
               (char*) pack->payload);
        exit(255);
    }

    // Will free payload
    rmc_sub_packet_dispatched(ctx, pack);

    // Is context ID within our expetcted range
    if (node_id >= expect_sz) {
        printf("rmc_proto_test_sub(): ContextID [%u] is out of range (0-%d)\n",
               node_id, expect_sz);
        exit(255);
     }

    // Is this context expected?
    if (expect[node_id].status == RMC_TEST_SUB_INACTIVE) {
        printf("rmc_proto_test_sub(): ContextID [%u] not expected. Use -e %u to setup subscriber expectations.\n",
               node_id, node_id);
        exit(255);
    }

    // Have we already completed all expected packages here?
    if (expect[node_id].status == RMC_TEST_SUB_COMPLETED) {
        printf("rmc_proto_test_sub(): ContextID [%u] have already processed its [%lu] packets. Got Current[%lu] Max[%lu].\n",
               node_id, expect[node_id].max_received, current, max_expected);
        exit(255);
    }

    // Check if this is the first packet from an expected source.
    // If so, set things up.
    if (expect[node_id].status == RMC_TEST_SUB_NOT_STARTED) {
        expect[node_id].status = RMC_TEST_SUB_IN_PROGRESS;
        expect[node_id].max_expected = max_expected;
        expect[node_id].max_received = 0; 
        
        printf("rmc_proto_test_sub(): Activate: node_id[%u] current[%lu] max_expected[%lu].\n",
               node_id, current, max_expected);

        // Fall through to the next if statement
    }

    // Check if we are in progress.
    // If so, check that packets are correctly numbrered.

    if (expect[node_id].status == RMC_TEST_SUB_IN_PROGRESS) {
        // Check that max_expected hasn't changed.
        if (max_expected != expect[node_id].max_expected) {
            printf("rmc_proto_test_sub(): ContextID [%u] max_expected changed from [%lu] to [%lu]\n",
                   node_id, expect[node_id].max_expected, max_expected);
            exit(255);
        }

        
        // Check that packet is consecutive.
        if (current != expect[node_id].max_received + 1) {
            printf("rmc_proto_test_sub(): ContextID [%u] Wanted[%lu] Got[%lu]\n",
                   node_id, expect[node_id].max_received + 1, current);
            exit(255);
        }

        expect[node_id].max_received = current;
        
        // Check if we are complete
        if (current == max_expected) {
            printf("rmc_proto_test_sub(): ContextID [%u] Complete at[%lu]\n",
                   node_id, current);
            
            expect[node_id].status = RMC_TEST_SUB_COMPLETED;
            // Check if this is the last one out.
            if (check_exit_condition(expect, expect_sz))
                return 0;
        }
        
        return 1;
    }

    printf("rmc_proto_test_sub(): Eh? expect[%u:%lu:%lu] status[%d]  data[%u:%lu:%lu]\n",
           node_id, expect[node_id].max_received, expect[node_id].max_expected,
           expect[node_id].status,
           node_id, current, max_expected);

    exit(255);
}


static int process_events(rmc_sub_context_t* ctx,
                          int epollfd,
                          usec_timestamp_t timeout_ts)
{
    struct epoll_event events[RMC_MAX_CONNECTIONS];
    char buf[16];
    int nfds = 0;

    nfds = epoll_wait(epollfd, events, RMC_MAX_CONNECTIONS, (timeout_ts == -1)?-1:(timeout_ts / 1000) + 1);
    if (nfds == -1) {
        perror("epoll_wait");
        exit(255);
    }

    // Timeout
    if (nfds == 0) 
        return ETIME;


    // printf("poll_wait(): %d results\n", nfds);

    while(nfds--) {
        int res = 0;
        uint8_t op_res = 0;
        rmc_index_t c_ind = events[nfds].data.u32;

//        printf("poll_wait(%s:%d)%s%s%s\n",
//               _index(c_ind, buf), _descriptor(ctx, c_ind),
//               ((events[nfds].events & EPOLLIN)?" read":""),
//               ((events[nfds].events & EPOLLOUT)?" write":""),
//               ((events[nfds].events & EPOLLHUP)?" disconnect":""));

        // Figure out what to do.
        if (events[nfds].events & EPOLLHUP) {
            _test("rmc_proto_test[%d.%d] process_events():rmc_close_tcp(): %s\n",
                  1, 1, rmc_conn_shutdown_connection(&ctx->conn_vec, c_ind));
            continue;
        }

        if (events[nfds].events & EPOLLIN) {
            errno = 0;
            res = rmc_sub_read(ctx, c_ind, &op_res);
            // Did we read a loopback message we sent ourselves?
//            printf("process_events(%s):%s\n", _op_res_string(op_res), strerror(res));
            if (res == ELOOP)
                continue;       

            _test("rmc_proto_test[%d.%d] process_events():rmc_read(): %s\n", 1, 1, res);
                
            // If this was a connection call processed, we can continue.
            if (op_res == RMC_READ_ACCEPT)
                continue;
        }

        if (events[nfds].events & EPOLLOUT) {
            _test("rmc_proto_test[%d.%d] process_events():rmc_write(): %s\n",
                  1, 10,
                  rmc_sub_write(ctx, c_ind, &op_res));
        }
    }

    return 0;
}



void test_rmc_proto_sub(char* mcast_group_addr,
                        char* mcast_if_addr,
                        int mcast_port,
                        rmc_context_id_t node_id,
                        uint8_t* node_id_map,
                        int node_id_map_size)
{
    rmc_sub_context_t* ctx = 0;
    int res = 0;
    int send_sock = 0;
    int send_ind = 0;
    int rec_sock = 0;
    int rec_ind = 0;
    int epollfd = -1;
    pid_t sub_pid = 0;
    user_data_t ud = { .u64 = 0 };
    int mode = 0;
    int ind = 0;
    usec_timestamp_t timeout_ts = 0;
    usec_timestamp_t exit_ts = 0;
    uint8_t *conn_vec_mem = 0;
    int do_exit = 0;

    // Indexed by publisher node_id
    sub_expect_t expect[node_id_map_size];

    signal(SIGHUP, SIG_IGN);

    epollfd = epoll_create1(0);
    for (ind = 0; ind < node_id_map_size; ++ind) {
        expect[ind].status = RMC_TEST_SUB_INACTIVE;
        expect[ind].max_received = 0;
        expect[ind].max_expected = 0;

        // Check if we are expecting traffic on this one
        if (node_id_map[ind]) 
            expect[ind].status = RMC_TEST_SUB_NOT_STARTED;
    }
    
    if (epollfd == -1) {
        perror("epoll_create1");
        exit(255);
    }

    ctx = malloc(sizeof(rmc_sub_context_t));
    
    conn_vec_mem = malloc(sizeof(rmc_connection_t)*RMC_MAX_CONNECTIONS);
    memset(conn_vec_mem, 0, sizeof(rmc_connection_t)*RMC_MAX_CONNECTIONS);
    rmc_sub_init_context(ctx,
                         0, // Assign random context id
                         mcast_group_addr,
                         mcast_if_addr,
                         mcast_port,
                         (user_data_t) { .i32 = epollfd },
                         poll_add, poll_modify, poll_remove,
                         conn_vec_mem, RMC_MAX_CONNECTIONS,
                         0,0);

    _test("rmc_proto_test_sub[%d.%d] activate_context(): %s",
          1, 1,
          rmc_sub_activate_context(ctx));

    
    printf("rmc_proto_test_sub: context: ctx[%.9X] mcast_addr[%s] mcast_port[%d] \n",
           rmc_sub_context_id(ctx), mcast_group_addr, mcast_port);


    while(1) {
        sub_packet_t* pack = 0;
        packet_id_t first_pid = 0;
        packet_id_t last_pid = 0;
        usec_timestamp_t current_ts = rmc_usec_monotonic_timestamp();
        
        rmc_sub_timeout_get_next(ctx, &timeout_ts);
        printf("timeout[%ld]\n", (timeout_ts == -1)?-1:timeout_ts  - current_ts);
        if (process_events(ctx, epollfd, timeout_ts) == ETIME) {
            puts("Yep");
            rmc_sub_timeout_process(ctx);
        }
        rmc_sub_timeout_process(ctx);

        // Process as many packets as possible.
        puts("Intro");
        
        while((pack = rmc_sub_get_next_dispatch_ready(ctx))) {
            if (!first_pid)
                first_pid = pack->pid;

            last_pid = pack->pid;
            if (!process_incoming_data(ctx, pack, expect, node_id_map_size)) {
                do_exit = 1;
                puts("EXIT");
                break;
            }
        }
        printf("Pid[%lu:%lu]\n", first_pid, last_pid);
        puts("Exit");

        if (do_exit)
            break;
    }

    puts("Shutting down");
    rmc_sub_shutdown_context(ctx);

    while(1) {
        rmc_sub_timeout_get_next(ctx, &timeout_ts);
        printf("timeout_ts[%ld]\n", timeout_ts - rmc_usec_monotonic_timestamp());

        if (timeout_ts == -1) 
            break;

        if (process_events(ctx, epollfd, timeout_ts) == ETIME)  {
            puts("Timed out");
            rmc_sub_timeout_process(ctx);
        }
    }
    
    puts("Done");
}
