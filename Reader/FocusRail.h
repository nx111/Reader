#pragma once
#include "framework.h"
BOOL ReaderFocusRailLayout(HWND owner,HWND pane,int width,int height,BOOL focus);
void ReaderFocusRailFollow();
void ReaderFocusRailEmphasis(BOOL clear);
BOOL ReaderFocusRailContains(HWND window);
void ReaderFocusRailDestroy();
