/*
 * COPYRIGHT:   See COPYING in the top level directory
 * PROJECT:     ReactOS Logon User Interface Host
 * FILE:        base/system/logonui/logonui.c
 * PROGRAMMERS: Ged Murphy (gedmurphy@reactos.org)
 */

#include "logonui.h"

/* DATA **********************************************************************/

#define IDC_CMD_BUTTON 1001

/* GLOBALS ******************************************************************/

PINFO g_pInfo = NULL;

/* FUNCTIONS ****************************************************************/

static LRESULT CALLBACK
WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_CMD_BUTTON)
            {
                ShellExecuteW(hwnd, L"open", L"cmd.exe", NULL, NULL, SW_SHOWNORMAL);
            }
            break;
        case WM_DESTROY:
            PostQuitMessage(0);
            break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static VOID
Run(VOID)
{
    WNDCLASSEXW wc = {0};
    HWND hwnd;
    MSG msg;

    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = g_pInfo->hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = L"LogonUI";
    RegisterClassExW(&wc);

    hwnd = CreateWindowExW(0, L"LogonUI", L"LogonUI",
                          WS_OVERLAPPEDWINDOW,
                          CW_USEDEFAULT, CW_USEDEFAULT, 320, 120,
                          NULL, NULL, g_pInfo->hInstance, NULL);
    if (!hwnd) return;

    CreateWindowW(L"BUTTON", L"Command Prompt",
                  WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
                  10, 10, 280, 40,
                  hwnd, (HMENU)IDC_CMD_BUTTON, g_pInfo->hInstance, NULL);

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    while (GetMessageW(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

int WINAPI
wWinMain(IN HINSTANCE hInst,
         IN HINSTANCE hPrevInstance,
         IN LPWSTR lpszCmdLine,
         IN int nCmdShow)
{
    LPWSTR *lpArgs;
    INT NumArgs;

    /* Allocate memory for the data */
    g_pInfo = (PINFO)HeapAlloc(GetProcessHeap(),
                                HEAP_ZERO_MEMORY,
                                sizeof(INFO));
    if (!g_pInfo) return -1;

    g_pInfo->hInstance = hInst;

    /* Get the command line args */
    lpArgs = CommandLineToArgvW(lpszCmdLine, &NumArgs);
    if (lpArgs)
    {
        /* Start the main routine */
        Run();
    }

    /* Free the data */
    HeapFree(GetProcessHeap(),
              0,
              g_pInfo);

    return 0;
}

/* EOF */