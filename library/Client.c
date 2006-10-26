/*
  BGI library implementation for Microsoft(R) Windows(TM)
  Copyright (C) 2006  Daniil Guitelson

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include "BGI.h"
#include "IPC.h"
#include "graphics.h"
#include <stdio.h>
#include <assert.h>
#include <process.h>
#include <string.h>
#include <stdlib.h>

static struct
{
  HWND wnd;
  HDC dc;
  int width, height;
  int mode;
} window;

static SHARED_STRUCT * sharedStruct;
static SHARED_OBJECTS sharedObjects;
static PAGE pages[2];
static HANDLE serverCheckerThread;

void openSharedObjects(void)
{
  int i;
  sharedObjects.keyboardEvent = IPC_openEvent(KEYBOARD_MUTEX_NAME);
  sharedObjects.serverPresentMutex = IPC_openMutex(SERVER_PRESENT_MUTEX_NAME);

  for(i = 0; i != 2; i++)
    sharedObjects.pagesSection[i] = IPC_openSection(PAGES_SECTION_NAME[i]);

  sharedStruct = IPC_openSharedMemory(SHARED_STRUCT_NAME, sizeof(SHARED_STRUCT));
  BGI_palette = IPC_openSharedMemory(PALETTE_SECTION_NAME, sizeof(RGBQUAD)*16);
  assert(sharedStruct != NULL);
}

static void serverPresenceChecker(DWORD w)
{
  IPC_lockMutex(sharedObjects.serverPresentMutex);
  ExitProcess(0);
}

static void serverThread(DWORD p)
{
  BGI_server(window.width, window.height, window.mode);
}

void BGI_startServer(int width, int height, int mode)
{
  int pc;
  wchar_t sparams[128];
  LPWSTR * params = CommandLineToArgvW(GetCommandLineW(), & pc);
  window.width = width;
  window.height = height;
  window.mode = mode;
  
  sharedObjects.clientPresentMutex = IPC_createMutex(CLIENT_PRESENT_MUTEX_NAME, TRUE);
  sharedObjects.serverCreatedEvent = IPC_createEvent(SERVER_STARTED_EVENT_NAME);
  if(mode & MODE_RELEASE)
  {
    HANDLE h = CreateThread(NULL,  0, (LPTHREAD_START_ROUTINE)serverThread, NULL, 0, 0);
    printf("");
  }
  else
  {
#if _MSC_VER >= 1400
    swprintf_s(sparams, sizeof(sparams) / sizeof(wchar_t), L"SERVER %i %i %i",width, height,mode);
#else
    _swprintf(sparams, L"SERVER %i %i %i",width, height,mode);
#endif
    _wspawnlp(_P_NOWAIT, params[0],L"SERVER",sparams,NULL);
  }
  
  IPC_waitEvent(sharedObjects.serverCreatedEvent);

  window.wnd = FindWindow(WINDOW_CLASS_NAME, NULL);
  window.dc = GetDC(window.wnd);

  openSharedObjects();
  serverCheckerThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)serverPresenceChecker, NULL, 0, NULL);
  
  for(pc = 0; pc != 2; pc++)
    BGI_createPage(pages + pc, window.dc,sharedObjects.pagesSection[pc], width, height);
}

PAGE * BGI_getPages(void)
{
  return pages;
}

void BGI_updateWindow(void)
{
  BitBlt(window.dc, 0, 0, window.width, window.height, pages[sharedStruct->visualPage].dc, 0, 0, SRCCOPY);
}

void BGI_setVisualPage(int page)
{
  sharedStruct->visualPage = page;
//  if(mode & MODE_SHOW_INVISIBLE_PAGE)
//    SendMessage(window.wnd, WM_VISUALPAGE_CHANGED, 0, 0);
}

SHARED_STRUCT * BGI_getSharedStruct()
{
  return sharedStruct;
}

HDC BGI_getWindowDC()
{
  return window.dc;
}

HWND BGI_getWindow()
{
  return window.wnd;
}

int BGI_waitForKeyPressed(void)
{
  int c;
  while(sharedStruct->keyCode == -1)
    Sleep(1);
  c = sharedStruct->keyCode;
  SendMessage(window.wnd, WM_KEYPROCESSED, 0, 0);
  return c;
}

static int translateKeyCode(int code)
{
  switch(code)
  {
  case VK_ESCAPE:
    return KEY_ESCAPE;
  case VK_SPACE:
    return ' ';
  case VK_SELECT:
    return '\n';
  case VK_INSERT:
    return(KEY_INSERT);
  case VK_HOME:
    return(KEY_HOME);
  case VK_END:
    return(KEY_END);
  case VK_LEFT:
    return(KEY_LEFT);
  case VK_RIGHT:
    return(KEY_RIGHT);
  case VK_UP:
    return(KEY_UP);
  case VK_DOWN:
    return(KEY_DOWN);
  case VK_TAB:
    return(KEY_TAB);
  case VK_BACK:
    return(KEY_BACK);
  }
  return code;
}

int BGI_getch()
{
  static int lastKey = -1;
  int c;

  if(lastKey != -1)
  {
    c = lastKey;
    lastKey = -1;
    SendMessage(window.wnd, WM_KEYPROCESSED, 0, 0);
    return translateKeyCode(c);
  }

  IPC_waitEvent(sharedObjects.keyboardEvent);

  if(sharedStruct->keyCode > 0)
  {
    lastKey = sharedStruct->keyCode;
    return 0;
  }

  c = sharedStruct->keyLetter;
  SendMessage(window.wnd, WM_KEYPROCESSED, 0, 0);
  return c;
}

void BGI_closeWindow()
{
  TerminateThread(serverCheckerThread, 0);
  SendMessage(window.wnd, WM_DESTROY, 0, 0);
  BGI_closeSharedObjects(&sharedObjects, sharedStruct);
}

