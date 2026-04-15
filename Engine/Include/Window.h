// Engine/Include/Window.h
#pragma once
#include <windows.h>
#include <string>

class Window {
public:
    Window(int width, int height, const std::wstring& title);
    ~Window();

    bool ProcessMessages();
    HWND GetHWND() const { return hWnd; }

private:
    static LRESULT CALLBACK WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    HWND hWnd;
    HINSTANCE hInstance;
};