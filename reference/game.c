#include <math.h>
#include "cprocessing.h"
#include "game.h"
#include "mainmenu.h"
#include "utils.h"

/* Define a type for the four compass directions */
typedef enum
{
	DIR_UP,
	DIR_DOWN,
	DIR_LEFT,
	DIR_RIGHT
} Direction;

/* Define a Player type holding two players' state */
typedef struct
{
	float x; // player x position (pixels)
	float y; // player y position (pixels)
	CP_Color color;
	Direction facing; // eye trangle direction (one of the four compass directions)
} Player;

#define PLAYER_DIAMETER    65.0f                    
#define PLAYER_SPEED       300.0f                    
#define AGENT_SPEED        300.0f                    
#define AGENT_AVOID_RANGE  150.0f                 
#define AGENT_EDGE_MARGIN  0.0f  // Trigger distance from window's edge to start turning away

static Player players[2]; /* The two players; shared across functions */
static int controlled_index; /* Index (0 or 1) of the player currently under keyboard control */

// Direction -> unit movement vector
static void Direction_ToVector(Direction dir, float* outX, float* outY)
{
	switch (dir)
	{
	case DIR_UP:    *outX = 0.0f;  *outY = -1.0f; break;
	case DIR_DOWN:  *outX = 0.0f;  *outY = 1.0f;  break;
	case DIR_LEFT:  *outX = -1.0f; *outY = 0.0f;  break;
	case DIR_RIGHT: *outX = 1.0f;  *outY = 0.0f;  break;
	default:        *outX = 0.0f;  *outY = 0.0f;  break;
	}
}

// Moves a Player along a unit vector, scaled by speed and dt
static void Player_Move(Player* p, float moveX, float moveY, float speed, float dt)
{
	p->x += moveX * speed * dt;
	p->y += moveY * speed * dt;
}

/* Convert a direction to the rotation angle of its icon */
static float FacingToDegrees(Direction facing)
{
	switch (facing)
	{
	case DIR_UP:    return 0.0f;
	case DIR_RIGHT: return 90.0f;  // rotates 90 degrees clockwise               
	case DIR_DOWN:  return 180.0f; // rotates 180 degrees clockwise 
	case DIR_LEFT:  return 270.0f; // rotates 270 degrees clockwise  
	}

	// In case facing is invalid (should never happen), return 0 degrees
	return 0.0f;
}


/* Draw one player as a coloured circle with an eye */
static void DrawPlayer(const Player* player)
{
	float radius = PLAYER_DIAMETER / 2.0f;

	// Draw the body circle first (bold outline), then fill color
	CP_Settings_Stroke(CP_Color_Create(0, 0, 0, 255));
	CP_Settings_StrokeWeight(3.0f);
	CP_Settings_Fill(player->color);             
	CP_Graphics_DrawCircle(player->x, player->y, PLAYER_DIAMETER);  /* Draw the body circle at the player centre */

	// Draw the eye triangle on top of the body circle
	CP_Settings_Stroke(CP_Color_Create(0, 0, 0, 255));
	CP_Settings_StrokeWeight(3.0f);
	CP_Settings_Fill(CP_Color_Create(255, 255, 255, 255)); // fill the triangle eye with white color

	/* x = player's middle horizontal position, y = player's middle vertical position */
	CP_Graphics_DrawTriangleAdvanced(
		player->x, player->y - (radius * 0.8f),                    // top vertex (middle of the circle, above the centre)
		player->x - (radius * 0.7f), player->y + (radius * 0.5f),  // Bottom-left vertex
		player->x + (radius * 0.7f), player->y + (radius * 0.5f),  // Bottom-right vertex
		FacingToDegrees(player->facing));                          // rotate the triangle to point in the direction the player is facing
}
void Game_Init(void)
{
	float window_width = (float)CP_System_GetWindowWidth();
	float window_height = (float)CP_System_GetWindowHeight();

	// place the two players in the window, give them colours and initial facing directions
	players[0].x = window_width * 0.25f;         // place the first player at 35% of the window width
	players[0].y = window_height * 0.2f;         // place the first player at 20% of the window height
	players[0].color = CP_Color_Create(0, 220, 0, 255);  // green colour 
	players[0].facing = DIR_UP;                  // Point the first player's eye up initially

	players[1].x = window_width * 0.35f;         // Place the second player at 35% of the window width
	players[1].y = window_height * 0.2f;         // Place the second player at 20% of the window height
	players[1].color = CP_Color_Create(220, 0, 0, 255);  // red color
	players[1].facing = DIR_LEFT;                  // Point the second player's eye up left initially

	controlled_index = 0; // start with 1st player (green)
}

void Game_Update(void)
{
	float dt = CP_System_GetDt();
	float window_width = (float)CP_System_GetWindowWidth();
	float window_height = (float)CP_System_GetWindowHeight();
	float radius = PLAYER_DIAMETER / 2.0f;
	int uncontrolled_index = 1 - controlled_index;
	int i;

	// gray background color
	CP_Graphics_ClearBackground(CP_Color_Create(200, 200, 200, 255));

	/* Switch which Player is controlled by clicking on it */
	if (CP_Input_MouseClicked())
	{
		float mouse_x = CP_Input_GetMouseX();
		float mouse_y = CP_Input_GetMouseY();

		if (IsCircleClicked(players[uncontrolled_index].x, players[uncontrolled_index].y, PLAYER_DIAMETER, mouse_x, mouse_y))
		{
			controlled_index = uncontrolled_index;
			uncontrolled_index = 1 - controlled_index;
		}
	}

	/* Move the controlled Player with WASD */
	{
		Player* controlled = &players[controlled_index];
		float move_x = 0.0f;
		float move_y = 0.0f;

		if (CP_Input_KeyDown(KEY_W))
		{
			move_y -= 1.0f; // going up
			controlled->facing = DIR_UP;
		}
		if (CP_Input_KeyDown(KEY_S))
		{
			move_y += 1.0f; // going down
			controlled->facing = DIR_DOWN;
		}
		if (CP_Input_KeyDown(KEY_A))
		{
			move_x -= 1.0f; // going left
			controlled->facing = DIR_LEFT;
		}
		if (CP_Input_KeyDown(KEY_D))
		{
			move_x += 1.0f; // going right
			controlled->facing = DIR_RIGHT;
		}

		// updates the position of the controlled player based on the movement direction, speed, and delta time
		controlled->x += move_x * PLAYER_SPEED * dt;
		controlled->y += move_y * PLAYER_SPEED * dt;
	}

	/* Move the uncontrolled Player to whatever the Triangle points to in the last direction moved */
	{
		Player* agent = &players[uncontrolled_index];
		float move_x, move_y;
		Direction_ToVector(agent->facing, &move_x, &move_y);
		Player_Move(agent, move_x, move_y, PLAYER_SPEED, dt);
	}

	/* Move Agent face away from player && window boarder when it is near */
	{
		Player* controlled = &players[controlled_index]; // The player the agent reacts to
		Player* agent = &players[uncontrolled_index];    // The AI-controlled player
		float dx = agent->x - controlled->x;             // distance between player and agent for honizontal x-axis
		float dy = agent->y - controlled->y;             // distance between player and agent for vertical y-axis
		float distance = sqrtf((dx * dx) + (dy * dy));
		int has_direction = 0;                           // if true (1), change direction for agent. Else false (0), agent stay at this direction.
		Direction new_facing = agent->facing;

		// if agent is near player, flee in opposite direction.
		if (distance < AGENT_AVOID_RANGE && distance > 0.0f)
		{
			/* Face away from the controlled Player to avoid collision */
			// fabsf() = floating-point absolute value.
			// if x-axis more than y-axis (If the player and agent are close enough)
			if (fabsf(dx) > fabsf(dy))
			{
				new_facing = (dx > 0.0f) ? DIR_RIGHT : DIR_LEFT; // agent flee right if player on the left, else agent flee left
			}
			else
			{
				new_facing = (dy > 0.0f) ? DIR_DOWN : DIR_UP;    // agent flee down if player on top, else agent flee upwards
			}
			has_direction = 1;
		}
		else if (agent->x < radius + AGENT_EDGE_MARGIN)
		{
			/* Near the left boundary, turn away from it */
			new_facing = DIR_RIGHT;
			has_direction = 1;
		}
		else if (agent->x > window_width - radius - AGENT_EDGE_MARGIN)
		{
			/* Near the right boundary, turn away from it */
			new_facing = DIR_LEFT;
			has_direction = 1;
		}
		else if (agent->y < radius + AGENT_EDGE_MARGIN)
		{
			/* Near the top boundary, turn away from it */
			new_facing = DIR_DOWN;
			has_direction = 1;
		}
		else if (agent->y > window_height - radius - AGENT_EDGE_MARGIN)
		{
			/* Near the bottom boundary, turn away from it */
			new_facing = DIR_UP;
			has_direction = 1;
		}

		// if true, change direction.
		if (has_direction)
		{
			float move_x, move_y;
			agent->facing = new_facing;
			Direction_ToVector(agent->facing, &move_x, &move_y);
			Player_Move(agent, move_x, move_y, AGENT_SPEED, dt);
		}
		/* else: no valid direction (e.g. trapped in a corner), so the agent does not move */
	}

	/* Keep both Players fully inside the window */
	for (i = 0; i < 2; ++i)
	{
		if (players[i].x < radius) players[i].x = radius;
		if (players[i].x > window_width - radius) players[i].x = window_width - radius;
		if (players[i].y < radius) players[i].y = radius;
		if (players[i].y > window_height - radius) players[i].y = window_height - radius;
	}

	/* Draw the uncontrolled Player first so the controlled Player renders above it */
	DrawPlayer(&players[uncontrolled_index]);
	DrawPlayer(&players[controlled_index]);

	if (CP_Input_KeyTriggered(KEY_Q))
	{
		CP_Engine_SetNextGameState(Main_Menu_Init, Main_Menu_Update, Main_Menu_Exit);
	}
}

void Game_Exit(void)
{
}