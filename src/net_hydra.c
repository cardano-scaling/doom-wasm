//
// Copyright(C) 2021 Cloudflare - celso@cloudflare.com
//
// DESCRIPTION:
//      Websockets network module for Chocolate Doom Wasm

#include <stdio.h>
#include <stdlib.h>

#include "debug.h"
#include "doom/doomdef.h"
#include "doom/doomstat.h"
#include "doomtype.h"
#include "i_system.h"
#include "m_argv.h"
#include "m_misc.h"
#include "net_defs.h"
#include "net_packet.h"
#include "net_hydra.h"
#include "z_zone.h"

#include "doom/hydra.h"

#define MAX_QUEUE_SIZE 64

extern uint32_t instanceUID;

// Hydra packet queue
typedef struct {
    net_packet_t *packets[MAX_QUEUE_SIZE];
    uint32_t froms[MAX_QUEUE_SIZE];
    int head, tail;
} packet_queue_t;

typedef struct {
    net_packet_t *packet;
    uint32_t *from;
} hydra_packet_t;

static packet_queue_t inbound_queue;

static void HydraQueueInit(packet_queue_t *queue) { queue->head = queue->tail = 0; }

static void HydraQueuePush(packet_queue_t *queue, net_packet_t *packet, uint32_t from)
{
    int new_tail;

    new_tail = (queue->tail + 1) % MAX_QUEUE_SIZE;

    if (new_tail == queue->head) {
        // queue is full
        return;
    }

    queue->packets[queue->tail] = packet;
    queue->froms[queue->tail] = from;
    queue->tail = new_tail;
}

static hydra_packet_t current_packet;
static hydra_packet_t *HydraQueuePop(packet_queue_t *queue) {
    if (queue->tail == queue->head) {
        // queue empty
        return NULL;
    }

    current_packet.packet = queue->packets[queue->head];
    current_packet.from = &queue->froms[queue->head];
    queue->head = (queue->head + 1) % MAX_QUEUE_SIZE;

    return &current_packet;
}

EMSCRIPTEN_KEEPALIVE
void ReceivePacket(uint32_t from, char *data, size_t len) {
    net_packet_t *packet = NET_NewPacket(len);
    memcpy(packet->data, data, len);
    packet->len = len;

    HydraQueuePush(&inbound_queue, packet, from);
}


// addresses table
static int addrs_index = 0;
net_addr_t addrs[MAX_QUEUE_SIZE];
uint32_t ips[MAX_QUEUE_SIZE];

static boolean NET_Hydra_InitClient(void)
{
    printf("doom: hydra: init client\n");
    return true;
}

static uint32_t to_ip;

static boolean NET_Hydra_InitServer(void)
{
    printf("doom: hydra: init server\n");
    HydraQueueInit(&inbound_queue);
    return true;
}

static uint32_t packets_sent = 0;

static void NET_Hydra_SendPacket(net_addr_t *addr, net_packet_t *packet)
{
    char *hydra_packet;
    int kills[MAXPLAYERS];
    int *hydra_kills;


    if (!addr->handle) {
        return;
    }
    to_ip = (*(uint32_t *)(addr->handle));

    hydra_packet = malloc(packet->len);
    memcpy(hydra_packet, packet->data, packet->len);

    for (int i=0; i < MAXPLAYERS; i++) {
        kills[i] = 0;
        for (int j=0; j< MAXPLAYERS; j++) {
            if (j == i) {
                kills[i] -= players[i].frags[j];
            } else {
            kills[i] += players[i].frags[j];
            }
        }
    }

    hydra_kills = malloc(sizeof(kills));
    memcpy(hydra_kills, kills, sizeof(kills));
    hydra_send_packet(to_ip, instanceUID, kills, sizeof(kills), hydra_packet, packet->len);
    packets_sent++;
}

static net_addr_t *FindAddressByIp(uint32_t ip)
{
    // looks for address in the addresses table, or create a new one
    // this should be a circular queue, but it's not, it's an hacky version
    // i'm assuming there won't be more than MAX_QUEUE_SIZE address during one gaming session

    // do we have it?
    for (int i = 0; i < MAX_QUEUE_SIZE; i++) {
        if (ips[i] == ip) {
            return (&addrs[i]);
        }
    }
    // nope, we need a new one
    if (addrs_index >= MAX_QUEUE_SIZE) {
        printf("doom: 3, we're out of client addresses\n");
        return (0);
    }
    else {
        addrs[addrs_index].refcount = 1;
        addrs[addrs_index].module = &net_hydra_module;
        ips[addrs_index] = ip;
        addrs[addrs_index].handle = &ips[addrs_index];
        return (&addrs[addrs_index++]);
    }
}

static boolean NET_Hydra_RecvPacket(net_addr_t **addr, net_packet_t **packet)
{
    hydra_packet_t *popped;

    popped = HydraQueuePop(&inbound_queue);

    if (popped != NULL) {
        *packet = popped->packet;
        *addr = FindAddressByIp((*(uint32_t *)(popped->from)));
        return true;
    }

    return false;
}

static void NET_Hydra_AddrToString(net_addr_t *addr, char *buffer, int buffer_len)
{
    M_snprintf(buffer, buffer_len, "hydra client %u", (*(uint32_t *)(addr->handle)));
}

static void NET_Hydra_FreeAddress(net_addr_t *addr) { free(addr); }

// this is only used to resolve the server address - hacky
static net_addr_t *NET_Hydra_ResolveAddress(const char *address)
{
    return FindAddressByIp((uint32_t)atoi(address));
}

net_module_t net_hydra_module = {
    NET_Hydra_InitClient,   NET_Hydra_InitServer,  NET_Hydra_SendPacket,     NET_Hydra_RecvPacket,
    NET_Hydra_AddrToString, NET_Hydra_FreeAddress, NET_Hydra_ResolveAddress,
};
