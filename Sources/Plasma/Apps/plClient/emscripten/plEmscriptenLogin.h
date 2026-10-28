/*==LICENSE==*

CyanWorlds.com Engine - MMOG client, server and tools
Copyright (C) 2011  Cyan Worlds, Inc.

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

Additional permissions under GNU GPL version 3 section 7

If you modify this Program, or any covered work, by linking or
combining it with any of RAD Game Tools Bink SDK, Autodesk 3ds Max SDK,
NVIDIA PhysX SDK, Microsoft DirectX SDK, OpenSSL library, Independent
JPEG Group JPEG library, Microsoft Windows Media SDK, or Apple QuickTime SDK
(or a modified version of those libraries),
containing parts covered by the terms of the Bink SDK EULA, 3ds Max EULA,
PhysX SDK EULA, DirectX SDK EULA, OpenSSL and SSLeay licenses, IJG
JPEG Library README, Windows Media SDK EULA, or QuickTime SDK EULA, the
licensors of this Program grant you additional
permission to convey the resulting work. Corresponding Source for a
non-source form of such a combination shall include the source code for
the parts of OpenSSL and IJG JPEG Library used as well as that of the covered
work.

You can contact Cyan Worlds, Inc. by email legal@cyan.com
 or by snail mail at:
      Cyan Worlds, Inc.
      14617 N Newport Hwy
      Mead, WA   99021

*==LICENSE==*/

// The browser's login front end: the page counterpart of the native login windows
// (win32/winmain.cpp UruLoginDialogProc with IDD_URULOGIN_MAIN of win32/res/plClient.rc;
// Mac-Cocoa/PLSLoginWindowController.mm) and their remembered login (winmain.cpp LoadUserPass/SaveUserPass,
// PLSLoginController.mm PLSLoginParameters load/save). The flow itself (the retry loop, the authentication) is
// main.cpp's, as it is winmain.cpp's; this file holds the page dialog, the record and the auth-failed text. The helpers
// the front ends share (the hash, the fake password, the auth-failed strings) are plClientLogin.h's.

#ifndef plEmscriptenLogin_inc
#define plEmscriptenLogin_inc

#include <string_theory/string>

#include "HeadSpin.h"
#include "pnEncryption/plChecksum.h"
#include "pnNetBase/pnNbError.h"

namespace plEmscriptenLogin
{
// One shard's remembered login: win32 keeps LastAccountName and RememberPassword in the registry under the
// server's display name and the password in pfPasswordStore (winmain.cpp SaveUserPass). The browser
// keeps the same two values and, instead of the password, its hash (namePassHash, the value StoreHash
// (plClientLogin.h) computes and NetCommSetAccountUsernamePassword takes): plaintext is never stored. File:
// <user data>/WebLogin/<shard>.dat (OPFS when the user folder is), outside the Init folder the harness collects.
struct Record
{
    ST::string username;
    bool       remember = false;
    bool       hasHash = false;
    ShaDigest  namePassHash{};
};
bool LoadRecord(const ST::string& shard, Record& record);
bool SaveRecord(const ST::string& shard, const Record& record);

// The text of IDD_AUTHFAILED (win32/res/plClient.rc) for an auth error, as one message-box text: its two lines
// (AuthFailedStrings, plClientLogin.h) are IDC_AUTH_TEXT and IDC_AUTH_MESSAGE of the dialog (winmain.cpp
// AuthFailedDialogProc).
ST::string AuthFailedText(ENetError authError);

// The control that has the focus when the dialog opens (winmain.cpp LoadUserPass).
enum class Focus
{
    kAccount,
    kPassword,
    kLogin
};

struct DialogInit
{
    ST::string username; // IDC_URULOGIN_USERNAME
    bool       remember; // IDC_URULOGIN_REMEMBERPASS; also puts FAKE_PASS_STRING in the password field
    Focus      focus;
    ST::string statusUrl; // fetched for IDC_STATUS_TEXT while the dialog is open (UruLoginDialogProc's
                          // status thread, WM_INITDIALOG)
    ST::string signupUrl; // IDC_URULOGIN_NEWACCTLINK, enabled when set (UruLoginDialogProc)
    ST::string product;   // IDC_PRODUCTSTRING (UruLoginDialogProc)
};

enum class Event
{
    kNone,
    kLogin,
    kQuit,
    kCancel
};

// All four run on the page thread (the call is proxied and waited for); the client thread never touches
// the DOM. Take returns the player's last action once: the typed values for kLogin (never logged),
// kQuit for Quit, kCancel for the "Logging in" box's Cancel.
void  Show(const DialogInit& init);
Event Take(ST::string& username, ST::string& password, bool& remember);
// Waits up to timeoutMs for the player to act in the dialog (Login, Quit, or the busy box's
// Cancel) and returns whether an action has been recorded since the last Take: the page's handlers wake the client
// thread, so the action is taken at once, as a modal dialog's WM_COMMAND is. Client thread only.
bool  WaitForPlayer(double timeoutMs);
void  SetBusy(bool busy); // IDD_AUTHENTICATING over the dialog (win32/res/plClient.rc)
void  Close();
} // namespace plEmscriptenLogin

#endif // plEmscriptenLogin_inc
