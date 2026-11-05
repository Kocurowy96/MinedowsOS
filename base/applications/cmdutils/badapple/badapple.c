/*
 * PROJECT:     MinedowsOS Bad Apple!! player
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Plays Bad Apple!!!1! as ASCII art in the console
 * COPYRIGHT:   Copyright 2026 Kocurowy96
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "resource.h"

#define BAD_APPLE_HEADER_SIZE 24
#define BAD_APPLE_VERSION 1
#define BAD_APPLE_MAX_DIMENSION 1024
#define BAD_APPLE_ATTRIBUTES (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY)
#define BAD_APPLE_LEVELS (ARRAYSIZE(Ramp) - 1)

typedef struct _BAD_APPLE_VIDEO
{
    ULONG Width;
    ULONG Height;
    ULONG RateNumerator;
    ULONG RateDenominator;
    ULONG FrameCount;
    PUCHAR Stream;
    SIZE_T StreamSize;
    SIZE_T Offset;
    PUCHAR Pixels;
} BAD_APPLE_VIDEO, *PBAD_APPLE_VIDEO;

typedef struct _BAD_APPLE_SCREEN
{
    SMALL_RECT Window;
    ULONG Columns;
    ULONG Rows;
    ULONG PictureLeft;
    ULONG PictureTop;
    ULONG PictureColumns;
    ULONG PictureRows;
    ULONG FontWidth;
    ULONG FontHeight;
    CHAR_INFO *Cells;
    PULONG ColumnStart;
    PULONG RowStart;
} BAD_APPLE_SCREEN, *PBAD_APPLE_SCREEN;

static const WCHAR Ramp[] = L" .:-=+*#%@";
static volatile LONG StopRequested;

static
ULONG
ReadLittleEndian(
    _In_reads_bytes_(Size) const UCHAR *Data,
    _In_ ULONG Size)
{
    ULONG Value = 0;

    while (Size--)
        Value = (Value << 8) | Data[Size];

    return Value;
}

static
BOOL
LoadVideo(
    _Out_ PBAD_APPLE_VIDEO Video)
{
    HRSRC Resource;
    HGLOBAL Global;
    const UCHAR *Data;
    DWORD Size;
    uLongf Length;

    ZeroMemory(Video, sizeof(*Video));

    Resource = FindResourceW(NULL, MAKEINTRESOURCEW(IDR_BADAPPLE), RT_RCDATA);
    if (!Resource)
        return FALSE;

    Size = SizeofResource(NULL, Resource);
    Global = LoadResource(NULL, Resource);
    Data = Global ? LockResource(Global) : NULL;
    if (!Data || Size < BAD_APPLE_HEADER_SIZE || memcmp(Data, "BAPL", 4) || ReadLittleEndian(Data + 4, 2) != BAD_APPLE_VERSION)
        return FALSE;

    Video->Width = ReadLittleEndian(Data + 6, 2);
    Video->Height = ReadLittleEndian(Data + 8, 2);
    Video->RateNumerator = ReadLittleEndian(Data + 10, 2);
    Video->RateDenominator = ReadLittleEndian(Data + 12, 2);
    Video->FrameCount = ReadLittleEndian(Data + 16, 4);
    Video->StreamSize = ReadLittleEndian(Data + 20, 4);
    if (!Video->Width || Video->Width > BAD_APPLE_MAX_DIMENSION ||
        !Video->Height || Video->Height > BAD_APPLE_MAX_DIMENSION ||
        !Video->RateNumerator || !Video->RateDenominator ||
        !Video->FrameCount || !Video->StreamSize)
    {
        return FALSE;
    }

    Video->Stream = malloc(Video->StreamSize);
    Video->Pixels = calloc(Video->Width * Video->Height, 1);
    if (!Video->Stream || !Video->Pixels)
        return FALSE;

    Length = (uLongf)Video->StreamSize;
    return uncompress(Video->Stream, &Length, Data + BAD_APPLE_HEADER_SIZE, Size - BAD_APPLE_HEADER_SIZE) == Z_OK &&
           Length == Video->StreamSize;
}

static
BOOL
ReadRun(
    _Inout_ PBAD_APPLE_VIDEO Video,
    _Out_ PULONG Run)
{
    ULONG Value = 0;
    ULONG Shift = 0;
    UCHAR Byte;

    do
    {
        if (Video->Offset >= Video->StreamSize || Shift > 28)
            return FALSE;

        Byte = Video->Stream[Video->Offset++];
        Value |= (ULONG)(Byte & 0x7F) << Shift;
        Shift += 7;
    } while (Byte & 0x80);

    *Run = Value;
    return TRUE;
}

static
BOOL
DecodeFrame(
    _Inout_ PBAD_APPLE_VIDEO Video)
{
    ULONG Total = Video->Width * Video->Height;
    ULONG Position = 0;
    ULONG Index;
    ULONG Run;
    UCHAR Mode;
    UCHAR Value = 0;

    if (Video->Offset >= Video->StreamSize)
        return FALSE;

    Mode = Video->Stream[Video->Offset++];
    if (Mode > 1)
        return FALSE;

    while (Position < Total)
    {
        if (!ReadRun(Video, &Run) || Run > Total - Position)
            return FALSE;

        if (!Mode)
        {
            memset(Video->Pixels + Position, Value, Run);
        }
        else if (Value)
        {
            for (Index = Position; Index < Position + Run; Index++)
                Video->Pixels[Index] ^= 1;
        }

        Position += Run;
        Value ^= 1;
    }

    return TRUE;
}

static
BOOL
UpdateLayout(
    _Inout_ PBAD_APPLE_SCREEN Screen,
    _In_ HANDLE Output,
    _In_ PBAD_APPLE_VIDEO Video)
{
    CONSOLE_SCREEN_BUFFER_INFO Info;
    CHAR_INFO *Cells;
    PULONG ColumnStart;
    PULONG RowStart;
    LONG Columns;
    LONG Rows;
    ULONG PictureColumns;
    ULONG PictureRows;
    ULONG Index;

    if (!GetConsoleScreenBufferInfo(Output, &Info))
        return FALSE;

    if (Screen->Cells && !memcmp(&Info.srWindow, &Screen->Window, sizeof(Info.srWindow)))
        return TRUE;

    Columns = Info.srWindow.Right - Info.srWindow.Left + 1;
    Rows = Info.srWindow.Bottom - Info.srWindow.Top + 1;
    if (Columns <= 0 || Rows <= 0)
        return FALSE;

    PictureColumns = Columns;
    PictureRows = (ULONG)(((ULONGLONG)Columns * Screen->FontWidth * Video->Height) /
                          ((ULONGLONG)Screen->FontHeight * Video->Width));
    if (PictureRows > (ULONG)Rows)
    {
        PictureRows = Rows;
        PictureColumns = (ULONG)(((ULONGLONG)Rows * Screen->FontHeight * Video->Width) /
                                 ((ULONGLONG)Screen->FontWidth * Video->Height));
    }
    PictureColumns = min(max(PictureColumns, 1), (ULONG)Columns);
    PictureRows = max(PictureRows, 1);

    Cells = realloc(Screen->Cells, (SIZE_T)Columns * Rows * sizeof(CHAR_INFO));
    if (!Cells)
        return FALSE;
    Screen->Cells = Cells;

    ColumnStart = realloc(Screen->ColumnStart, (PictureColumns + 1) * sizeof(ULONG));
    if (!ColumnStart)
        return FALSE;
    Screen->ColumnStart = ColumnStart;

    RowStart = realloc(Screen->RowStart, (PictureRows + 1) * sizeof(ULONG));
    if (!RowStart)
        return FALSE;
    Screen->RowStart = RowStart;

    for (Index = 0; Index < (ULONG)(Columns * Rows); Index++)
    {
        Cells[Index].Char.UnicodeChar = L' ';
        Cells[Index].Attributes = BAD_APPLE_ATTRIBUTES;
    }

    for (Index = 0; Index <= PictureColumns; Index++)
        ColumnStart[Index] = Index * Video->Width / PictureColumns;

    for (Index = 0; Index <= PictureRows; Index++)
        RowStart[Index] = Index * Video->Height / PictureRows;

    Screen->Window = Info.srWindow;
    Screen->Columns = Columns;
    Screen->Rows = Rows;
    Screen->PictureColumns = PictureColumns;
    Screen->PictureRows = PictureRows;
    Screen->PictureLeft = (Columns - PictureColumns) / 2;
    Screen->PictureTop = (Rows - PictureRows) / 2;
    return TRUE;
}

static
BOOL
RenderFrame(
    _Inout_ PBAD_APPLE_SCREEN Screen,
    _In_ HANDLE Output,
    _In_ PBAD_APPLE_VIDEO Video)
{
    CHAR_INFO *Cell;
    SMALL_RECT Region = Screen->Window;
    COORD Origin = { 0, 0 };
    COORD Size;
    ULONG Row;
    ULONG Column;
    ULONG X;
    ULONG Y;
    ULONG Left;
    ULONG Right;
    ULONG Top;
    ULONG Bottom;
    ULONG Lit;
    ULONG Total;

    for (Row = 0; Row < Screen->PictureRows; Row++)
    {
        Top = Screen->RowStart[Row];
        Bottom = max(Screen->RowStart[Row + 1], Top + 1);
        Cell = &Screen->Cells[(Screen->PictureTop + Row) * Screen->Columns + Screen->PictureLeft];

        for (Column = 0; Column < Screen->PictureColumns; Column++, Cell++)
        {
            Left = Screen->ColumnStart[Column];
            Right = max(Screen->ColumnStart[Column + 1], Left + 1);

            Lit = 0;
            for (Y = Top; Y < Bottom; Y++)
            {
                for (X = Left; X < Right; X++)
                    Lit += Video->Pixels[Y * Video->Width + X];
            }

            Total = (Right - Left) * (Bottom - Top);
            Cell->Char.UnicodeChar = Ramp[(Lit * (BAD_APPLE_LEVELS - 1) * 2 + Total) / (Total * 2)];
        }
    }

    Size.X = (SHORT)Screen->Columns;
    Size.Y = (SHORT)Screen->Rows;
    return WriteConsoleOutputW(Output, Screen->Cells, Size, Origin, &Region);
}

static
BOOL
KeyPressed(
    _In_ HANDLE Input)
{
    INPUT_RECORD Records[16];
    DWORD Count;
    DWORD Index;

    if (Input == INVALID_HANDLE_VALUE)
        return FALSE;

    while (GetNumberOfConsoleInputEvents(Input, &Count) && Count)
    {
        if (!ReadConsoleInputW(Input, Records, ARRAYSIZE(Records), &Count))
            return FALSE;

        for (Index = 0; Index < Count; Index++)
        {
            if (Records[Index].EventType == KEY_EVENT &&
                Records[Index].Event.KeyEvent.bKeyDown &&
                (Records[Index].Event.KeyEvent.wVirtualKeyCode == VK_ESCAPE ||
                 Records[Index].Event.KeyEvent.wVirtualKeyCode == 'Q'))
            {
                return TRUE;
            }
        }
    }

    return FALSE;
}

static
BOOL
WINAPI
CtrlHandler(
    _In_ DWORD CtrlType)
{
    if (CtrlType != CTRL_C_EVENT && CtrlType != CTRL_BREAK_EVENT)
        return FALSE;

    InterlockedExchange(&StopRequested, 1);
    return TRUE;
}

static
PCWSTR
Play(
    _Inout_ PBAD_APPLE_VIDEO Video,
    _Inout_ PBAD_APPLE_SCREEN Screen,
    _In_ HANDLE Output,
    _In_ HANDLE Input)
{
    LARGE_INTEGER Frequency;
    LARGE_INTEGER Start;
    LARGE_INTEGER Now;
    ULONGLONG Due;
    LONGLONG Next;
    ULONG Decoded = 0;

    QueryPerformanceFrequency(&Frequency);
    QueryPerformanceCounter(&Start);

    while (!InterlockedCompareExchange(&StopRequested, 0, 0) && !KeyPressed(Input))
    {
        QueryPerformanceCounter(&Now);
        Due = (ULONGLONG)(Now.QuadPart - Start.QuadPart) * Video->RateNumerator /
              ((ULONGLONG)Frequency.QuadPart * Video->RateDenominator);
        if (Due >= Video->FrameCount)
            break;

        if (Due >= Decoded)
        {
            while (Decoded <= Due)
            {
                if (!DecodeFrame(Video))
                    return L"the embedded video is damaged";
                Decoded++;
            }

            if (!UpdateLayout(Screen, Output, Video) || !RenderFrame(Screen, Output, Video))
                return L"drawing to the console failed";
        }

        Next = Start.QuadPart + (LONGLONG)(((ULONGLONG)Decoded * Frequency.QuadPart * Video->RateDenominator +
                                            Video->RateNumerator - 1) / Video->RateNumerator);
        QueryPerformanceCounter(&Now);
        if (Next > Now.QuadPart)
            Sleep((DWORD)(((ULONGLONG)(Next - Now.QuadPart) * 1000 + Frequency.QuadPart - 1) / Frequency.QuadPart));
    }

    return NULL;
}

int
wmain(
    _In_ int argc,
    _In_ WCHAR *argv[])
{
    BAD_APPLE_VIDEO Video;
    BAD_APPLE_SCREEN Screen;
    CONSOLE_FONT_INFO Font;
    CONSOLE_CURSOR_INFO Cursor;
    HANDLE Original;
    HANDLE Buffer;
    HANDLE Input;
    PCWSTR Error;

    if (argc > 1)
    {
        wprintf(L"Plays Bad Apple!! as ASCII art in the console.\n\n"
                L"BADAPPLE\n\n"
                L"Press Esc, Q or Ctrl+C to stop.\n");
        return wcscmp(argv[1], L"/?") ? 1 : 0;
    }

    if (!LoadVideo(&Video))
    {
        fwprintf(stderr, L"badapple: the embedded video is missing or damaged\n");
        free(Video.Stream);
        free(Video.Pixels);
        return 1;
    }

    Original = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    Buffer = CreateConsoleScreenBuffer(GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, CONSOLE_TEXTMODE_BUFFER, NULL);
    if (Original == INVALID_HANDLE_VALUE || Buffer == INVALID_HANDLE_VALUE || !SetConsoleActiveScreenBuffer(Buffer))
    {
        if (Buffer != INVALID_HANDLE_VALUE)
            CloseHandle(Buffer);
        if (Original != INVALID_HANDLE_VALUE)
            CloseHandle(Original);
        fwprintf(stderr, L"badapple: a console is required\n");
        free(Video.Stream);
        free(Video.Pixels);
        return 1;
    }

    ZeroMemory(&Screen, sizeof(Screen));
    Screen.FontWidth = 8;
    Screen.FontHeight = 16;
    if (GetCurrentConsoleFont(Buffer, FALSE, &Font) && Font.dwFontSize.X > 0 && Font.dwFontSize.Y > 0)
    {
        Screen.FontWidth = Font.dwFontSize.X;
        Screen.FontHeight = Font.dwFontSize.Y;
    }

    Cursor.dwSize = 1;
    Cursor.bVisible = FALSE;
    SetConsoleCursorInfo(Buffer, &Cursor);

    Input = CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    SetConsoleCtrlHandler(CtrlHandler, TRUE);

    Error = Play(&Video, &Screen, Buffer, Input);

    SetConsoleCtrlHandler(CtrlHandler, FALSE);
    SetConsoleActiveScreenBuffer(Original);
    if (Input != INVALID_HANDLE_VALUE)
        CloseHandle(Input);
    CloseHandle(Buffer);
    CloseHandle(Original);

    free(Screen.Cells);
    free(Screen.ColumnStart);
    free(Screen.RowStart);
    free(Video.Stream);
    free(Video.Pixels);

    if (Error)
    {
        fwprintf(stderr, L"badapple: %ls\n", Error);
        return 1;
    }

    return 0;
}
