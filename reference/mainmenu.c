
#include "cprocessing.h"
#include "utils.h"
#include <stdio.h>
#include "game.h"
CP_Font font;

#define BUTTON_WIDTH   300.0f
#define BUTTON_HEIGHT  200.0f
#define X_CENTER (CP_System_GetWindowWidth() * 0.5f)
#define Y_CENTER (CP_System_GetWindowHeight() * 0.5f)

void Main_Menu_Init(void)
{
    font = CP_Font_Load("Assets/Exo2-Regular.ttf");
    CP_Font_Set(font);

    CP_Settings_RectMode(CP_POSITION_CENTER);
    CP_Settings_TextAlignment(CP_TEXT_ALIGN_H_CENTER, CP_TEXT_ALIGN_V_MIDDLE);

    // front size 72.
    CP_Settings_TextSize(72.f);
}

void Main_Menu_Update(void)
{
	// make background color gray
    CP_Graphics_ClearBackground(CP_Color_Create(100, 100, 100, 255));

	// make rectangle border black color
    CP_Settings_Stroke(CP_Color_Create(0, 0, 0, 255));
    CP_Settings_StrokeWeight(2.0f);

    // make rectangle color red
	CP_Settings_Fill(CP_Color_Create(255, 120, 120, 255));
   
    
	// Play button rectangle
    // Format: CP_Graphics_DrawRect(x position, y position, width of rectangle, height of rectangle);
    CP_Graphics_DrawRect(X_CENTER, Y_CENTER - BUTTON_HEIGHT, BUTTON_WIDTH, BUTTON_HEIGHT);

	// Exit button rectangle
    CP_Graphics_DrawRect(X_CENTER, Y_CENTER + BUTTON_HEIGHT, BUTTON_WIDTH, BUTTON_HEIGHT);


    // make text black color
    CP_Settings_Fill(CP_Color_Create(0, 0, 0, 255));

	// play button text
    CP_Settings_TextAlignment(CP_TEXT_ALIGN_H_CENTER, CP_TEXT_ALIGN_V_TOP);
	// format: CP_Font_DrawText("text", x length, y height);
    CP_Font_DrawText("Play", X_CENTER, 210.0f);

	// Exit button text
    CP_Settings_TextAlignment(CP_TEXT_ALIGN_H_CENTER, CP_TEXT_ALIGN_V_BOTTOM);
    CP_Font_DrawText("Exit", X_CENTER, 685.0f);

	// area_center_x = 1600 / 2 = 800
	// area_center_y = 900 / 2 = 450
	// Check if user clicked on the play button
    //IsAreaClicked(float area_center_x, float area_center_y, float area_width, float area_height, float click_x, float click_y)
    if (CP_Input_MouseClicked())
    {
        if (IsAreaClicked(X_CENTER, Y_CENTER - BUTTON_HEIGHT, BUTTON_WIDTH, BUTTON_HEIGHT, CP_Input_GetMouseX(), CP_Input_GetMouseY()))
        {
           CP_Engine_SetNextGameState(Game_Init, Game_Update, Game_Exit);
        }

		// Check Exit Button
        else if (IsAreaClicked(X_CENTER, Y_CENTER + BUTTON_HEIGHT, BUTTON_WIDTH, BUTTON_HEIGHT, CP_Input_GetMouseX(), CP_Input_GetMouseY()))
        {
            CP_Engine_Terminate();
        }
       
    }
 
}

void Main_Menu_Exit(void)
{
    // Unload the font
    CP_Font_Free(font);
}

 