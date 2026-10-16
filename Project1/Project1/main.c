#include "cprocessing.h"
#include "menu.h"
#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>


int main(void) {
    _CrtSetDbgFlag ( _CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF );
    CP_Engine_SetNextGameState(menu_init, menu_update, menu_exit);
    CP_System_SetWindowSize(1600, 900);
    CP_Engine_Run(0); // opens console window (for debug print messages)
    return 0;
}