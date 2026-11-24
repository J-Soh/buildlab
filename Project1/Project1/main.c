#include "cprocessing.h"
#include "menu.h"
#include "utils.h"  // load_highscore: read highscore.txt before the first frame
#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>


int main(void) {
    _CrtSetDbgFlag ( _CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF );
    load_highscore();  // read highscore.txt before the menu draws its first frame
    CP_Engine_SetNextGameState(menu_init, menu_update, menu_exit);
    CP_Engine_Run(0); // opens console window (for debug print messages)
    return 0;
}