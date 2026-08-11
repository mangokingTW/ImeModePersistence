#pragma once

#include <windows.h>

#include <string>

// A small, click-through, always-on-top badge drawn next to the text caret,
// showing the current input language and conversion mode. It lives on the UI
// thread that created it; caret positions are resolved on a background thread
// (see caret.h) and handed here as screen rectangles.
namespace overlay {

// Registers the window class and creates the (hidden) badge window owned by
// owner. Returns false if the window could not be created.
bool init(HINSTANCE instance, HWND owner);

// Positions the badge next to caretScreen (a caret rectangle in screen pixels),
// sets its text and shows it without taking focus.
void show(const RECT& caretScreen, const std::wstring& text);

// Hides the badge without destroying it.
void hide();

// Destroys the badge window and frees its resources.
void destroy();

} // namespace overlay
