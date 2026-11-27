// Camelion++ plugin SDK
// Copyright (C) 2026 Camelion++ project
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// at your option any later version.
//
// ---------------------------------------------------------------------------
// The Camelion++ plugin ABI is deliberately modelled on Notepad++'s
// PluginInterface.h / Notepad_plus_msgs.h so that existing Notepad++ plugins
// can be ported to Linux with as few changes as possible:
//
//   * the same six-ish exported entry points (setInfo, getName, getFuncsArray,
//     beNotified, messageProc) plus two Camelion++ specific ones,
//   * the same NppData / FuncItem / ShortcutKey structures,
//   * the same NPPM_* message numbers and NPPN_* notification codes,
//   * Scintilla messages are sent to the Scintilla "handle" exactly as before.
//
// Differences from Notepad++ (Windows):
//   * All strings are UTF-8 `char`, never wchar_t (wchar_t is UTF-32 on Linux).
//   * There is no Win32 ::SendMessage. NppData carries a function pointer,
//     `_sendMessage`, that does the job. Define CAMELION_SENDMESSAGE_SHIM
//     before including this header to get a ::SendMessage() look-alike.
//   * Handles (HWND) are opaque tokens. Use CAMM_GETMAINWINDOW to obtain the
//     real GtkWindow* if your plugin uses GTK for dialogs, and CAMM_ADDPANEL to
//     dock a GtkWidget* into the side panel.
//   * ShortcutKey::_key keeps Windows virtual-key codes (A-Z, 0-9, VK_F1..F12)
//     so shortcut tables can be copied unchanged.
//
// A plugin is a shared object named  <FolderName>/<FolderName>.so  inside one
// of the plugin directories (see "Plugins > Open Plugins Folder").
// ---------------------------------------------------------------------------
#pragma once

#include <stdint.h>
#include <stddef.h>
#include "Scintilla.h"

#ifdef __cplusplus
extern "C" {
#endif

// ----------------------------------------------------------- Win32-ish types
#ifndef CAMELION_NO_WIN32_TYPES
typedef void*          HWND;
typedef unsigned int   UINT;
typedef uintptr_t      WPARAM;
typedef intptr_t       LPARAM;
typedef intptr_t       LRESULT;
#endif
#ifndef __cplusplus
#include <stdbool.h>
#endif
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#define CAMELION_PLUGIN_API_VERSION 1

#define CAMELION_EXPORT __attribute__((visibility("default")))
#ifdef __cplusplus
#define CAMELION_EXTERN_C extern "C"
#else
#define CAMELION_EXTERN_C
#endif
// Put this in front of each exported entry point:  CAMELION_PLUGIN_EXPORT void setInfo(NppData d) {...}
#define CAMELION_PLUGIN_EXPORT CAMELION_EXTERN_C CAMELION_EXPORT

typedef LRESULT (*CamSendMessageFn)(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

struct NppData
{
    HWND _nppHandle;                 // the editor itself: send NPPM_* here
    HWND _scintillaMainHandle;       // the main Scintilla view: send SCI_* here
    HWND _scintillaSecondHandle;     // second view (aliases the main view in 1.x)
    CamSendMessageFn _sendMessage;   // Camelion++: use instead of ::SendMessage
    int  _apiVersion;                // CAMELION_PLUGIN_API_VERSION of the host
};

typedef const char* (*PFUNCGETNAME)(void);
typedef void        (*PFUNCSETINFO)(struct NppData);
typedef void        (*PFUNCPLUGINCMD)(void);
typedef void        (*PBENOTIFIED)(struct SCNotification*);
typedef LRESULT     (*PMESSAGEPROC)(UINT Message, WPARAM wParam, LPARAM lParam);
typedef int         (*PFUNCAPIVERSION)(void);

struct ShortcutKey
{
    bool _isCtrl;
    bool _isAlt;
    bool _isShift;
    unsigned char _key;              // Windows VK code: 'A'..'Z', '0'..'9', 0x70..0x7B = F1..F12
};

#define menuItemSize 64

struct FuncItem
{
    char _itemName[menuItemSize];    // UTF-8. An empty name (or "---") inserts a separator
    PFUNCPLUGINCMD _pFunc;
    int  _cmdID;                     // filled in by Camelion++
    bool _init2Check;
    struct ShortcutKey* _pShKey;
};

typedef struct FuncItem* (*PFUNCGETFUNCSARRAY)(int*);

// --------------------------------------------------------------- entry points
// Every plugin must export these (extern "C").
//   void               setInfo(NppData);
//   const char*        getName(void);
//   FuncItem*          getFuncsArray(int* nbF);
//   void               beNotified(SCNotification*);
//   LRESULT            messageProc(UINT, WPARAM, LPARAM);
//   int                camelionPluginApiVersion(void);   // return CAMELION_PLUGIN_API_VERSION
// and should export the plugin version, used by Plugins Admin and validator.py:
//   const char camelionPluginVersion[] = "1.2.3";        // see CAMELION_PLUGIN_VERSION()

#define CAMELION_PLUGIN_VERSION(ver) \
    CAMELION_EXTERN_C CAMELION_EXPORT __attribute__((used)) const char camelionPluginVersion[] = ver; \
    CAMELION_EXTERN_C CAMELION_EXPORT int camelionPluginApiVersion(void) { return CAMELION_PLUGIN_API_VERSION; }

// ------------------------------------------------------------ language types
// Same numbering as Notepad++'s LangType enum.
enum LangType {
    L_TEXT, L_PHP, L_C, L_CPP, L_CS, L_OBJC, L_JAVA, L_RC,
    L_HTML, L_XML, L_MAKEFILE, L_PASCAL, L_BATCH, L_INI, L_ASCII, L_USER,
    L_ASP, L_SQL, L_VB, L_JS_EMBEDDED, L_CSS, L_PERL, L_PYTHON, L_LUA,
    L_TEX, L_FORTRAN, L_BASH, L_FLASH, L_NSIS, L_TCL, L_LISP, L_SCHEME,
    L_ASM, L_DIFF, L_PROPS, L_PS, L_RUBY, L_SMALLTALK, L_VHDL, L_KIX, L_AU3,
    L_CAML, L_ADA, L_VERILOG, L_MATLAB, L_HASKELL, L_INNO, L_SEARCHRESULT,
    L_CMAKE, L_YAML, L_COBOL, L_GUI4CLI, L_D, L_POWERSHELL, L_R, L_JSP,
    L_COFFEESCRIPT, L_JSON, L_JAVASCRIPT, L_FORTRAN_77, L_BAANC, L_SREC,
    L_IHEX, L_TEHEX, L_SWIFT, L_ASN1, L_AVS, L_BLITZBASIC, L_PUREBASIC,
    L_FREEBASIC, L_CSOUND, L_ERLANG, L_ESCRIPT, L_FORTH, L_LATEX, L_MMIXAL,
    L_NIM, L_NNCRONTAB, L_OSCRIPT, L_REBOL, L_REGISTRY, L_RUST, L_SPICE,
    L_TXT2TAGS, L_VISUALPROLOG, L_TYPESCRIPT, L_JSON5, L_MSSQL, L_GDSCRIPT,
    L_HOLLYWOOD, L_GOLANG, L_RAKU, L_TOML, L_SAS, L_ERRORLIST, L_ESCSEQ, L_STTXT,
    // Camelion++ additions (not in Notepad++)
    L_MARKDOWN_CAM = 0x1000,
    L_EXTERNAL = 0x2000
};

// -------------------------------------------------------------- messages
#define WM_USER 0x0400
#define NPPMSG  (WM_USER + 1000)

#define NPPM_GETCURRENTSCINTILLA      (NPPMSG + 4)   // lParam: int* out (0 = main view)
#define NPPM_GETCURRENTLANGTYPE       (NPPMSG + 5)   // lParam: int* out
#define NPPM_SETCURRENTLANGTYPE       (NPPMSG + 6)   // lParam: LangType
#define NPPM_GETNBOPENFILES           (NPPMSG + 7)   // lParam: ALL_OPEN_FILES / PRIMARY_VIEW / SECOND_VIEW
    #define ALL_OPEN_FILES 0
    #define PRIMARY_VIEW   1
    #define SECOND_VIEW    2
#define NPPM_CREATESCINTILLAHANDLE    (NPPMSG + 20)  // returns a new (hidden) Scintilla handle owned by the plugin
#define NPPM_DESTROYSCINTILLAHANDLE   (NPPMSG + 21)  // lParam: handle from NPPM_CREATESCINTILLAHANDLE
#define NPPM_GETCURRENTDOCINDEX       (NPPMSG + 23)  // lParam: view; returns index
    #define MAIN_VIEW 0
    #define SUB_VIEW  1
#define NPPM_SETSTATUSBAR             (NPPMSG + 24)  // wParam: field, lParam: const char*
    #define STATUSBAR_DOC_TYPE     0
    #define STATUSBAR_DOC_SIZE     1
    #define STATUSBAR_CUR_POS      2
    #define STATUSBAR_EOF_FORMAT   3
    #define STATUSBAR_UNICODE_TYPE 4
    #define STATUSBAR_TYPING_MODE  5
#define NPPM_GETMENUHANDLE            (NPPMSG + 25)  // wParam: NPPPLUGINMENU / NPPMAINMENU; returns GtkMenuShell*
    #define NPPPLUGINMENU 0
    #define NPPMAINMENU   1
#define NPPM_ACTIVATEDOC              (NPPMSG + 28)  // wParam: view, lParam: index
#define NPPM_RELOADFILE               (NPPMSG + 36)  // wParam: withAlert, lParam: const char* path
#define NPPM_SWITCHTOFILE             (NPPMSG + 37)  // lParam: const char* path
#define NPPM_SAVECURRENTFILE          (NPPMSG + 38)
#define NPPM_SAVEALLFILES             (NPPMSG + 39)
#define NPPM_SETMENUITEMCHECK         (NPPMSG + 40)  // wParam: cmdID, lParam: bool
#define NPPM_MAKECURRENTBUFFERDIRTY   (NPPMSG + 44)
#define NPPM_GETPLUGINSCONFIGDIR      (NPPMSG + 46)  // wParam: buf size, lParam: char* buf
#define NPPM_MSGTOPLUGIN              (NPPMSG + 47)  // wParam: const char* dest module name, lParam: CommunicationInfo*
#define NPPM_MENUCOMMAND              (NPPMSG + 48)  // lParam: IDM_* command id
#define NPPM_GETNPPVERSION            (NPPMSG + 50)  // returns MAKELONG(minor, major)
#define NPPM_HIDETABBAR               (NPPMSG + 51)
#define NPPM_ISTABBARHIDDEN           (NPPMSG + 52)
#define NPPM_GETPOSFROMBUFFERID       (NPPMSG + 57)
#define NPPM_GETFULLPATHFROMBUFFERID  (NPPMSG + 58)  // wParam: bufferID, lParam: char* (NULL => returns length)
#define NPPM_GETBUFFERIDFROMPOS       (NPPMSG + 59)  // wParam: index, lParam: view
#define NPPM_GETCURRENTBUFFERID       (NPPMSG + 60)
#define NPPM_RELOADBUFFERID           (NPPMSG + 61)
#define NPPM_GETBUFFERLANGTYPE        (NPPMSG + 64)
#define NPPM_SETBUFFERLANGTYPE        (NPPMSG + 65)
#define NPPM_GETBUFFERENCODING        (NPPMSG + 66)  // 0 ANSI, 1 UTF-8 BOM, 2 UTF-16BE, 3 UTF-16LE, 4 UTF-8
#define NPPM_SETBUFFERENCODING        (NPPMSG + 67)
#define NPPM_GETBUFFERFORMAT          (NPPMSG + 68)  // 0 CRLF, 1 CR, 2 LF
#define NPPM_SETBUFFERFORMAT          (NPPMSG + 69)
#define NPPM_HIDETOOLBAR              (NPPMSG + 70)
#define NPPM_ISTOOLBARHIDDEN          (NPPMSG + 71)
#define NPPM_HIDEMENU                 (NPPMSG + 72)
#define NPPM_ISMENUHIDDEN             (NPPMSG + 73)
#define NPPM_HIDESTATUSBAR            (NPPMSG + 74)
#define NPPM_ISSTATUSBARHIDDEN        (NPPMSG + 75)
#define NPPM_DOOPEN                   (NPPMSG + 77)  // lParam: const char* path
#define NPPM_SAVECURRENTFILEAS        (NPPMSG + 78)  // wParam: asCopy, lParam: const char* path
#define NPPM_ALLOCATECMDID            (NPPMSG + 81)  // wParam: count, lParam: int* out first id
#define NPPM_ALLOCATEMARKER           (NPPMSG + 82)  // wParam: count, lParam: int* out first marker
#define NPPM_GETLANGUAGENAME          (NPPMSG + 83)  // wParam: LangType, lParam: char* (NULL => length)
#define NPPM_GETLANGUAGEDESC          (NPPMSG + 84)
#define NPPM_GETCURRENTVIEW           (NPPMSG + 88)
#define NPPM_GETEDITORDEFAULTFOREGROUNDCOLOR (NPPMSG + 90)
#define NPPM_GETEDITORDEFAULTBACKGROUNDCOLOR (NPPMSG + 91)
#define NPPM_SAVEFILE                 (NPPMSG + 94)  // lParam: const char* path
#define NPPM_GETPLUGINHOMEPATH        (NPPMSG + 97)  // wParam: size, lParam: char*
#define NPPM_ISAUTOINDENTON           (NPPMSG + 105)
#define NPPM_GETCURRENTMACROSTATUS    (NPPMSG + 106)
#define NPPM_ISDARKMODEENABLED        (NPPMSG + 107)
#define NPPM_GETBOOKMARKID            (NPPMSG + 111)
#define NPPM_ALLOCATEINDICATOR        (NPPMSG + 113) // wParam: count, lParam: int* out first
#define NPPM_GETNPPSETTINGSDIRPATH    (NPPMSG + 119) // wParam: size, lParam: char*

#define RUNCOMMAND_USER (WM_USER + 3000)
    #define VAR_NOT_RECOGNIZED 0
    #define FULL_CURRENT_PATH  1
    #define CURRENT_DIRECTORY  2
    #define FILE_NAME          3
    #define NAME_PART          4
    #define EXT_PART           5
    #define CURRENT_WORD       6
    #define NPP_DIRECTORY      7
    #define CURRENT_LINE       8
    #define CURRENT_COLUMN     9
    #define NPP_FULL_FILE_PATH 10
    #define GETFILENAMEATCURSOR 11
    #define CURRENT_LINESTR    12
#define NPPM_GETFULLCURRENTPATH   (RUNCOMMAND_USER + FULL_CURRENT_PATH)   // wParam: size, lParam: char*
#define NPPM_GETCURRENTDIRECTORY  (RUNCOMMAND_USER + CURRENT_DIRECTORY)
#define NPPM_GETFILENAME          (RUNCOMMAND_USER + FILE_NAME)
#define NPPM_GETNAMEPART          (RUNCOMMAND_USER + NAME_PART)
#define NPPM_GETEXTPART           (RUNCOMMAND_USER + EXT_PART)
#define NPPM_GETCURRENTWORD       (RUNCOMMAND_USER + CURRENT_WORD)
#define NPPM_GETNPPDIRECTORY      (RUNCOMMAND_USER + NPP_DIRECTORY)
#define NPPM_GETNPPFULLFILEPATH   (RUNCOMMAND_USER + NPP_FULL_FILE_PATH)
#define NPPM_GETFILENAMEATCURSOR  (RUNCOMMAND_USER + GETFILENAMEATCURSOR)
#define NPPM_GETCURRENTLINESTR    (RUNCOMMAND_USER + CURRENT_LINESTR)
#define NPPM_GETCURRENTLINE       (RUNCOMMAND_USER + CURRENT_LINE)        // returns 0-based line
#define NPPM_GETCURRENTCOLUMN     (RUNCOMMAND_USER + CURRENT_COLUMN)      // returns 0-based column

// --------------------------------------------- Camelion++ specific messages
#define CAMMSG (NPPMSG + 500)
#define CAMM_GETMAINWINDOW   (CAMMSG + 1)   // returns GtkWindow*
#define CAMM_ADDPANEL        (CAMMSG + 2)   // wParam: const char* title, lParam: GtkWidget*; returns panel id
#define CAMM_SHOWPANEL       (CAMMSG + 3)   // wParam: panel id, lParam: bool show
#define CAMM_GETSCINTILLAWIDGET (CAMMSG + 4) // returns GtkWidget* of the current Scintilla
#define CAMM_GETVERSIONSTRING (CAMMSG + 5)  // wParam: size, lParam: char*

// Message to another plugin (NPPM_MSGTOPLUGIN)
struct CommunicationInfo
{
    long internalMsg;
    const char* srcModuleName;
    void* info;
};

// ---------------------------------------------------------- notifications
// Delivered to beNotified() with nmhdr.code set to one of these,
// nmhdr.hwndFrom = _nppHandle and nmhdr.idFrom = buffer ID (where relevant).
#define NPPN_FIRST 1000
#define NPPN_READY              (NPPN_FIRST + 1)
#define NPPN_TBMODIFICATION     (NPPN_FIRST + 2)
#define NPPN_FILEBEFORECLOSE    (NPPN_FIRST + 3)
#define NPPN_FILEOPENED         (NPPN_FIRST + 4)
#define NPPN_FILECLOSED         (NPPN_FIRST + 5)
#define NPPN_FILEBEFOREOPEN     (NPPN_FIRST + 6)
#define NPPN_FILEBEFORESAVE     (NPPN_FIRST + 7)
#define NPPN_FILESAVED          (NPPN_FIRST + 8)
#define NPPN_SHUTDOWN           (NPPN_FIRST + 9)
#define NPPN_BUFFERACTIVATED    (NPPN_FIRST + 10)
#define NPPN_LANGCHANGED        (NPPN_FIRST + 11)
#define NPPN_WORDSTYLESUPDATED  (NPPN_FIRST + 12)
#define NPPN_FILEBEFORELOAD     (NPPN_FIRST + 14)
#define NPPN_READONLYCHANGED    (NPPN_FIRST + 16)
#define NPPN_BEFORESHUTDOWN     (NPPN_FIRST + 19)
#define NPPN_DARKMODECHANGED    (NPPN_FIRST + 27)
#define NPPN_GLOBALMODIFIED     (NPPN_FIRST + 30)

// ------------------------------------------------------- menu command IDs
// Subset of Notepad++'s menuCmdID.h, accepted by NPPM_MENUCOMMAND.
#define IDM            40000
#define IDM_FILE       (IDM + 1000)
#define IDM_FILE_NEW       (IDM_FILE + 1)
#define IDM_FILE_OPEN      (IDM_FILE + 2)
#define IDM_FILE_CLOSE     (IDM_FILE + 3)
#define IDM_FILE_CLOSEALL  (IDM_FILE + 4)
#define IDM_FILE_SAVE      (IDM_FILE + 6)
#define IDM_FILE_SAVEALL   (IDM_FILE + 7)
#define IDM_FILE_SAVEAS    (IDM_FILE + 8)
#define IDM_FILE_EXIT      (IDM_FILE + 11)
#define IDM_FILE_RELOAD    (IDM_FILE + 14)
#define IDM_EDIT       (IDM + 2000)
#define IDM_EDIT_CUT       (IDM_EDIT + 1)
#define IDM_EDIT_COPY      (IDM_EDIT + 2)
#define IDM_EDIT_UNDO      (IDM_EDIT + 3)
#define IDM_EDIT_REDO      (IDM_EDIT + 4)
#define IDM_EDIT_PASTE     (IDM_EDIT + 5)
#define IDM_EDIT_DELETE    (IDM_EDIT + 6)
#define IDM_EDIT_SELECTALL (IDM_EDIT + 7)
#define IDM_EDIT_DUP_LINE  (IDM_EDIT + 10)
#define IDM_EDIT_UPPERCASE (IDM_EDIT + 16)
#define IDM_EDIT_LOWERCASE (IDM_EDIT + 17)
#define IDM_SEARCH     (IDM + 3000)
#define IDM_SEARCH_FIND     (IDM_SEARCH + 1)
#define IDM_SEARCH_REPLACE  (IDM_SEARCH + 3)
#define IDM_SEARCH_GOTOLINE (IDM_SEARCH + 4)
#define IDM_VIEW       (IDM + 4000)
#define IDM_VIEW_WRAP      (IDM_VIEW + 22)
#define IDM_VIEW_ZOOMIN    (IDM_VIEW + 23)
#define IDM_VIEW_ZOOMOUT   (IDM_VIEW + 24)

#ifdef __cplusplus
} // extern "C"
#endif

// ------------------------------------------------ ::SendMessage look-alike
// For plugins ported from Notepad++. Call camelionInitSendMessage(nppData)
// in setInfo(), then ::SendMessage(nppData._nppHandle, NPPM_..., w, l) works.
#if defined(__cplusplus) && defined(CAMELION_SENDMESSAGE_SHIM)
inline CamSendMessageFn& camelionSendMessagePtr() { static CamSendMessageFn p = nullptr; return p; }
inline void camelionInitSendMessage(const NppData& d) { camelionSendMessagePtr() = d._sendMessage; }
inline LRESULT SendMessage(HWND h, UINT m, WPARAM w = 0, LPARAM l = 0)
{
    CamSendMessageFn f = camelionSendMessagePtr();
    return f ? f(h, m, w, l) : 0;
}
#endif
