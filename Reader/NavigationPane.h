#pragma once
#include "framework.h"
const UINT WM_READER_NAVIGATION_ACCESS = WM_APP + 732;
const UINT WM_READER_NATIVE_MENU_CLOSED = WM_APP + 733;
const UINT WM_READER_NAVIGATION_REFRESH = WM_APP + 736;
BOOL CreateReaderNavigation(HWND owner, HMENU menu);
HWND ReaderNavigationWindow();
HMENU ReaderMenuHandle(HWND owner);
void LayoutReaderNavigation(int width, int height, BOOL visible, BOOL panel, BOOL focus = FALSE);
void RefreshReaderNavigationEmphasis();
void FollowReaderNavigation();
BOOL ReaderNavigationForeground();
BOOL ReaderNavigationKeyboardActive();
void LeaveReaderNavigation();
void ReaderNavigationMenuChanged();
void ReaderNavigationState(BOOL back, BOOL forward, BOOL editing, UINT active = 0, BOOL ready = TRUE);
BOOL ReaderNavigationTracking();
void ReaderNavigationMenuClosed();
BOOL ReaderNavigationWheel(UINT message, WPARAM wParam, LPARAM lParam);
BOOL ReaderNavigationTranslate(MSG* message);
BOOL ReaderNavigationContains(HWND window);
void CancelReaderNavigation();
BOOL ReaderNavigationSystemMenu(WPARAM wParam, LPARAM lParam);
void DestroyReaderNavigation();
