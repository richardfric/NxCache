#pragma once
#include <windows.h>

extern char szPath[MAX_PATH];

DWORD install_win_service();
DWORD remove_win_service();
