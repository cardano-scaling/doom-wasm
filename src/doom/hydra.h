#ifndef __HYDRA__
#define __HYDRA__

#include <stdio.h>
#include <emscripten.h>

#include "d_ticcmd.h"
#include "d_player.h"
#include "doomdef.h"
#include "doomstat.h"
#include "doomtype.h"
#include "net_defs.h"

EM_ASYNC_JS(void, hydra_send_packet, (uint32_t to, uint32_t from, uint32_t *kills, size_t kills_len, char *packet, size_t len), {
    let data = HEAPU8.subarray(packet, packet + len);
    let kills = HEAPU8.subarray(kills, kills + kills_len);
    let g = typeof window !== "undefined" ? window : global;
    let hydra = !!g ? g.HydraMultiplayer : null;
    if (!!hydra) {
        await hydra.SendPacket(to, from, kills, data);
    }
})

#endif
