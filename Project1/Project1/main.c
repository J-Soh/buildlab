#include "cprocessing.h"
#include "menu.h"

int main(void) {
    CP_Engine_SetNextGameState(menu_init, menu_update, menu_exit);
    CP_System_SetWindowSize(1600, 900);
    CP_Engine_Run(1); // opens console window (for debug print messages)
    return 0;
}