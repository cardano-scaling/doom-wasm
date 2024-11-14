#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "d_player.h"
#include "doomdef.h"
#include "doomkeys.h"
#include "doomstat.h"
#include "i_system.h"
#include "d_loop.h"
#include "p_local.h"
#include "g_navigation.h"
#include "m_random.h"

#include "g_bot.h"
extern byte consistancy[MAXPLAYERS][BACKUPTICS];
extern fixed_t forwardmove[2];
extern fixed_t sidemove[2];
extern fixed_t angleturn[3];
extern int localplayer;
extern navpath_t main_path;

typedef enum {
    RESPAWN_DELAY = 50,
    TARGETING_TIMEOUT = 100,
    TARGETING_DELAY = 12,
    IGNORE_TIMEOUT = 250,
} bot_defaults_t;

typedef enum {
    JUST_SPAWNED,
    SEARCHING_FOR_NODES,
    DEATHMATCH_ROAM_PATH,
    DEATHMATCH_ROAM_REVERSE_PATH,
    ENGAGING_ENEMY,
    TRYING_TO_GET_UNSTUCK,
} bot_state_t;

typedef enum {
    PATROLLING,
} bot_orders_t;

typedef enum {
    CAREFUL,
    WALK,
    RUN,
} bot_speed_t;

typedef struct bot_s {
    bot_state_t state;
    bot_orders_t orders;
    player_t* self;

    // Respawn
    boolean respawn_now;
    int respawn_timeout;

    // Walking
    int meander_timeout;
    bot_speed_t speed;
    int forward;
    int strafe;
    int turn;
    int turn_duration;

    // Targeting
    int target_timeout;
    int target_delay;
    mobj_t* target;
    int target_prev_health;
    mobj_t* ignored_target;
    int ignore_timeout;

    // Attacking
    boolean shoot;
} bot_t;

bot_t bot;

void BOT_Monologue(const char *fmt, ...) {
    if (false) {
        return;
    }

    va_list args;

    printf("BOT: %8d: ", I_GetTimeMS());
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
}

void BOT_InitBot() {
    BOT_Monologue("init bot");
    memset(&bot, 0, sizeof(bot_t));
    bot.state = JUST_SPAWNED;
    bot.speed = RUN;
    bot.target_delay = 50; // Wait a bit at the beginning of the map to start shooting
}

void BOT_DeathThink() {
    BOT_Monologue("dead think");
    // First frame we die, set the timeout
    if (bot.respawn_timeout == 0) {
        bot.respawn_timeout = RESPAWN_DELAY;
    } else if (bot.respawn_timeout > 1) {
        bot.respawn_timeout--;
    } else {
        bot.respawn_timeout = 0;
        bot.state = JUST_SPAWNED;
        bot.respawn_now = true;
    }
}

void BOT_StartPatrolling() {
    BOT_Monologue("start patrolling");
    bot.state = SEARCHING_FOR_NODES;
    bot.orders = PATROLLING;
}

int BOT_SearchForNodes(navpath_t* path) {
	int node;	
	int max = path->total_nodes;

	for ( node = 0; node < max; node++ )
	{
		if ( Path_FoundNode(path, bot.self->mo, node) )
		{
			bot.self->mo->potential_node = node;

			return node;
		}
	}

	return 0;
}

int BOT_SearchForNodesFromEnd(navpath_t* path) {
    int node;
    int max = path->total_nodes;

    for (node = max; node > 0; node-- ) {
        if (Path_FoundNode(path, bot.self->mo, node)) {
            bot.self->mo->potential_node = node;
            return node;
        }
    }
    return 0;
}

void BOT_FollowPath(navpath_t* path) {
    BOT_Monologue("follow path");
}

void BOT_Walk() {
    switch(bot.speed) {
        case CAREFUL:
            bot.forward = forwardmove[0] * 0.5;
            break;
        case WALK:
            bot.forward = forwardmove[0];
            break;
        case RUN:
            bot.forward = forwardmove[0] * 1.5;
            break;
    }
}

// Institute a delay between acquiring new targets
boolean BOT_CheckTargetingDelay() {
    if (bot.target_delay) {
        bot.target_delay--;
        return true;
    } else {
        bot.target_delay = TARGETING_DELAY;
        return false;
    }
}

// Institute a timeout for any given target, so that if we can't damage them, we switch to a new target
boolean BOT_CheckTargetingTimeout() {
    if (bot.target->health <= 0) {
        return true;
    }

    if (bot.target_timeout) {
        bot.target_timeout--;
        return false;
    } else {
        if (bot.target_prev_health == bot.target->health) {
            // We haven't damaged the target in the timeout period, so try to select a new target
            bot.ignored_target = bot.target;
            bot.ignore_timeout = IGNORE_TIMEOUT;
        }

        bot.target_delay = TARGETING_DELAY * 5; // Wait a bit longer after we abandon a target
        bot.target_timeout = TARGETING_TIMEOUT;
        return true;
    }
}

boolean BOT_TargetInViewingAngle(mobj_t* target) {
    angle_t angle_to_target = R_PointToAngle2(
                                    bot.self->mo->x,
                                    bot.self->mo->y,
                                    target->x,
                                    target->y
                                );
    angle_t angle_diff = angle_to_target - bot.self->mo->angle;

    return angle_diff < ANG90 || angle_diff > ANG270; 
}

void BOT_TakeTarget() {
    if (bot.target == bot.ignored_target && bot.ignore_timeout) {
        return;
    }

    BOT_Monologue("target found");
    bot.state = ENGAGING_ENEMY;
    bot.self->mo->target = bot.target;
    // Turn to face target

    bot.target_prev_health = bot.target->health;
    bot.target_timeout = TARGETING_TIMEOUT;
}

boolean BOT_FindHostile() {
    if (bot.state == ENGAGING_ENEMY) {
        return false;
    }

    int offset = 0; // P_Random() % MAXPLAYERS;
    for(int i = 0; i < MAXPLAYERS; i++) {
        int player_i = (i + offset) % MAXPLAYERS;
        
        if (!playeringame[player_i]) {
            continue;
        }

        mobj_t* target = players[player_i].mo;
        player_t* player = &players[player_i];

        if (bot.self == player) {
            continue;
        }

        if (P_CheckSight(bot.self->mo, target)
            && BOT_TargetInViewingAngle(target)
            && target->health > 0
            && !player->powers[pw_invisibility]) {
                bot.target = target;
                BOT_TakeTarget();
                BOT_Monologue("found hostile");
        }
    }
}


void BOT_ChooseTarget() {
    if (BOT_CheckTargetingDelay()) {
        return;
    }

    if (BOT_FindHostile()) {
        return;
    }

    if (bot.self->attacker && bot.self->attacker->health > 0) {
        BOT_Monologue("under attack!");
        bot.target = bot.self->attacker;
        BOT_TakeTarget();
    }
}

boolean BOT_FaceTarget() {
    if (bot.state != ENGAGING_ENEMY) {
        return true;
    }

    angle_t angle_to_target = R_PointToAngle2(
                                    bot.self->mo->x,
                                    bot.self->mo->y,
                                    bot.target->x,
                                    bot.target->y
                                );
    fixed_t distance = P_AproxDistance(bot.self->mo->x - bot.target->x, bot.self->mo->y - bot.target->y);
    angle_t angle_diff = angle_to_target - bot.self->mo->angle;
    angle_t tolerance = ANG1 * 5;
    angle_t fine_tune = ANG1 * 20;

    // If we're within 5 degrees of the player, then don't try to turn any more
    if ((angle_diff > 0 && angle_diff < tolerance) || (angle_diff > ANG_MAX - tolerance && angle_diff < ANG_MAX)) {
        bot.turn = 0;
        bot.strafe = sidemove[0];
        return false;
    } else if (angle_diff > ANG180) {
        // Otherwise, if the player is to the right of our facing direction, turn right
        // (which corresponds to a negative turning direction)
        // If we're further than 20 degrees off, turn fast
        // Otherwise, turn slow
        if (angle_diff < ANG_MAX - fine_tune) {
            bot.turn = -angleturn[1];
        } else {
            bot.turn = -angleturn[2];
        }
        // Make sure we're very reactive
        bot.turn_duration = 0;
        // Lets also strafe to the left, to try to dodge the player
        // if the player is close, we try to oscillate back and forth
        // otherwise we try to circulate around the player
        bot.strafe = distance < 20000000 ? sidemove[1] : -sidemove[1];
    } else {
        // Otherwise, the player is to the left of our facing direction, so turn left
        // (which corresponds to a positive turning direction)
        // If we're further than 20 degrees off, turn fast
        // Otherwise turn slow
        if (angle_diff > fine_tune) {
            bot.turn = angleturn[1];
        } else {
            bot.turn = angleturn[2];
        }
        // And make sure we're reactive
        bot.turn_duration = 0;
        // Lets also strafe to the right, to try to dodge the player
        // if the player is close, we try to oscillate back and forth
        // otherwise we try to circulate around the player
        bot.strafe = distance < 20000000 ? -sidemove[1] : sidemove[1];
    } 
    return true;
}

void BOT_Attack() {
    if (bot.state != ENGAGING_ENEMY) {
        return;
    }

    if (bot.target->health <= 0 || bot.self->health <= 0) {
        BOT_Monologue("target or self dead, reverting");
        bot.state = SEARCHING_FOR_NODES;
        return;
    }

    if (BOT_CheckTargetingTimeout()) {
        BOT_Monologue("losing interest");
        bot.state = SEARCHING_FOR_NODES;
        return;
    }

    if (!P_CheckSight(bot.self->mo, bot.target)) {
        BOT_Monologue("lost sight of target");
        if (BOT_SearchForNodesFromEnd(&main_path)) {
            bot.state = DEATHMATCH_ROAM_PATH;
        } else {
            bot.state = SEARCHING_FOR_NODES;
        }
    }

    // Try to turn towards target
    if (BOT_FaceTarget()) {
        return;
    }

    // Check if we're in melee range
    // Otherwise, try to shoot
    bot.shoot = true;
}

fixed_t wall_distance;
boolean BOT_CheckWall(intercept_t *in) {
    if (in->d.line->flags & ML_TWOSIDED || in->d.line->flags & ML_BLOCKING) {
        if (wall_distance == -1 || in->frac < wall_distance) {
            wall_distance = in->frac;
        }
        return true;
    }

    return false;
}

fixed_t BOT_ProbeWallDistance(angle_t probe) {
    fixed_t pos_x = bot.self->mo->x;
    fixed_t pos_y = bot.self->mo->y;
    fixed_t forward_x = bot.self->mo->x + FixedMul(10 * 128 * FRACUNIT, finecosine[probe >> ANGLETOFINESHIFT]);
    fixed_t forward_y = bot.self->mo->y + FixedMul(10 * 128 * FRACUNIT, finesine[probe >> ANGLETOFINESHIFT]);

    wall_distance = -1;
    P_PathTraverse(pos_x, pos_y, forward_x, forward_y, PT_ADDLINES, BOT_CheckWall);
    return wall_distance;
}

boolean BOT_AvoidWalls() {
    angle_t facing_angle = bot.self->mo->angle;
    angle_t forward_pressure = 0;
    angle_t right_pressure = 0;
    angle_t left_pressure = 0;
 
    fixed_t min_dist = 1800;

    fixed_t forward = BOT_ProbeWallDistance(facing_angle);
    if (forward < min_dist) {
        forward_pressure += min_dist - forward;
    }

    // Probe to the left and right
    for (int i = 0; i < 9; i++) {
        angle_t probe = facing_angle + (i * 5 * ANG1);
        fixed_t left = BOT_ProbeWallDistance(probe);
        BOT_Monologue("left %d: %d", probe / ANG1, left);
        if (left < min_dist) {
            left_pressure += min_dist - left;
        }
        probe = facing_angle - (i * 5 * ANG1);
        fixed_t right = BOT_ProbeWallDistance(probe);
        BOT_Monologue("right %d: %d", probe / ANG1, left);
        if (right < min_dist) {
            right_pressure += min_dist - right;
        }
    }

    BOT_Monologue("facing %d, forward: %d, left: %d, right: %d", facing_angle / ANG1, forward_pressure, left_pressure, right_pressure);
    // If we have no forward pressure, and very little pressure on either side, we're free to wander
    if (forward_pressure == 0 && left_pressure < 200 && right_pressure < 200) {
        return false;
    }

    // If we have forward pressure, and also things on both sides, turn all the way around
    if (forward_pressure > 4000 && left_pressure > 1000 && right_pressure > 1000) {
        bot.turn = left_pressure > right_pressure ? angleturn[1] : -angleturn[1];
        bot.turn_duration = 20;
        return true;
    }

    if (left_pressure > right_pressure) {
        bot.turn = -angleturn[1];
        bot.turn_duration = 20;
    } else {
        bot.turn = angleturn[1];
        bot.turn_duration = 20;
    }

    return false;
}

void BOT_Meander() {
    // Don't meander if we're on a mission
    if (bot.state != SEARCHING_FOR_NODES && bot.state != TRYING_TO_GET_UNSTUCK) {
        return;
    }

    // This idea of "pressure" to try to keep the bot away from walls didn't work so well
    // if (BOT_AvoidWalls()) {
    //    return;
    // }


    if (bot.meander_timeout) {
        bot.meander_timeout--;
        BOT_Walk();
    }
    if (!bot.meander_timeout) {
        int hi = 50;
        int lo = 15;
        int middle = (hi + lo) / 2;
        
        bot.meander_timeout = M_Random() % hi + lo;

        if (bot.meander_timeout > middle) {
            bot.turn = angleturn[1];
            bot.turn_duration = M_Random() % 35 + 5;
        } else {
            bot.turn = -angleturn[1];
            bot.turn_duration = M_Random() % 35 + 5;
        }
    }
}

void BOT_Think() {
    // Find the player / player map object
    player_t* player = &players[consoleplayer];
    if (!player || !player->mo) {
        return;
    }
    bot.self = player;

    if (bot.ignore_timeout) {
        bot.ignore_timeout--;
    }

    // If we're dead, we need to try to respawn
    if (bot.self->playerstate == PST_DEAD) {
        BOT_DeathThink();
        return;
    }

    // TODO: what does this do?
	// if (!player->mo->subsector->sector->special)
	//   this_Bot.lava_timeout = BOT_LAVA_TIMEOUT;

    // handle some pathfinding cases
    switch(bot.state) {
        case JUST_SPAWNED:
            BOT_StartPatrolling();
            break;
        case SEARCHING_FOR_NODES:
            if (BOT_SearchForNodes(&main_path)) {
                bot.state = DEATHMATCH_ROAM_PATH;
            }
            break;
        case DEATHMATCH_ROAM_PATH:
        case DEATHMATCH_ROAM_REVERSE_PATH:
            if (bot.self->mo->current_node < main_path.total_nodes) {
                BOT_FollowPath(&main_path);
            } else {
                bot.state = SEARCHING_FOR_NODES;
            }
            break;
    }

    if (bot.state != ENGAGING_ENEMY) {
        bot.target_timeout = TARGETING_TIMEOUT * 3;
    }

    BOT_Meander();
    BOT_ChooseTarget();
    BOT_Attack();
}

void BOT_Act(ticcmd_t *cmd) {
    // BOT_Monologue("state: %d, forward: %d, strafe: %d, turn: %d, shoot: %s", bot.state, bot.forward, bot.strafe, bot.turn, bot.shoot ? "true" : "false");

    if (bot.respawn_now) {
        cmd->buttons |= BT_USE;
        bot.respawn_now = false;
    }

    if (bot.shoot) {
        cmd->buttons |= BT_ATTACK;
        bot.shoot = false;
    }

    cmd->forwardmove = bot.forward;
    cmd->sidemove = bot.strafe;
    cmd->angleturn = bot.turn;

    bot.forward = 0;
    bot.strafe = 0;
    if (bot.turn_duration) {
        bot.turn_duration--;
    } else {
        bot.turn = 0;
    }
}

// Ported from Marshmallow Doom
// https://github.com/drbelljazz/marshmallow-doom/blob/master/src/doom/bot_lib.c#L820
// https://github.com/drbelljazz/marshmallow-doom/blob/master/src/doom/bot_dm.c#L579
void BOT_BuildTiccmd(ticcmd_t *cmd, int maketic) {
    // Prepare the ticcmd for the bots actions
    memset(cmd, 0, sizeof(ticcmd_t));
    // Set the consistency check, which helps check for desyncs
    cmd->consistancy = consistancy[consoleplayer][maketic % BACKUPTICS];

    BOT_Think();
    BOT_Act(cmd);
}
