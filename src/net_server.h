//
// Copyright(C) 2005-2014 Simon Howard
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// Network server code
//

#ifndef NET_SERVER_H
#define NET_SERVER_H

// initialize server and wait for connections

#include <stdint.h>
void NET_SV_Init(void);

// run server: check for new packets received etc.

void NET_SV_Run(void);

// Shut down the server
// Blocks until all clients disconnect, or until a 5 second timeout

void NET_SV_Shutdown(void);

// Add a network module to the context used by the server

void NET_SV_AddModule(net_module_t *module);

int NET_SV_NumPlayers(void);

// Register server with master server.

void NET_SV_RegisterWithMaster(void);

void hydra_game_started();
void hydra_game_ended();
void hydra_player_connected(uint32_t addr, uint32_t player_num);
void hydra_player_disconnected(uint32_t addr, uint32_t player_num);

#endif /* #ifndef NET_SERVER_H */
