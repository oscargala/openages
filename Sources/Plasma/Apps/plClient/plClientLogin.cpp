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

#include "plClientLogin.h"

#include <regex>

#include "hsEndian.h"
#include "plResMgr/plLocalization.h"
#include "pnEncryption/plChallengeHash.h"

void StoreHash(const ST::string& username, const ST::string& password, ShaDigest namePassHash)
{
    //  Hash username and password before sending over the 'net.
    //  -- Legacy compatibility: @gametap (and other usernames with domains in them) need
    //     to be hashed differently.
    static const std::regex re_domain("[^@]+@([^.]+\\.)*([^.]+)\\.[^.]+");
    std::cmatch             match;
    std::regex_search(username.c_str(), match, re_domain);
    if (match.empty() || ST::string(match[2].str()).compare_i("gametap") == 0) {
        //  Plain Usernames...
        plChecksum shasum(
            plChecksum::Type::kSHA1,
            password.size(),
            reinterpret_cast<const uint8_t*>(password.c_str()));
        uint32_t*       dest = reinterpret_cast<uint32_t*>(namePassHash);
        const uint32_t* from = reinterpret_cast<const uint32_t*>(shasum.GetValue());

        dest[0] = hsToBE32(from[0]);
        dest[1] = hsToBE32(from[1]);
        dest[2] = hsToBE32(from[2]);
        dest[3] = hsToBE32(from[3]);
        dest[4] = hsToBE32(from[4]);
    } else {
        //  Domain-based Usernames...
        CryptHashPassword(username, password, namePassHash);
    }
}

void AuthFailedStrings(ENetError authError, const char** ppStr1, const char** ppStr2, const wchar_t** ppWStr)
{
    *ppStr1 = nullptr;
    *ppStr2 = nullptr;
    *ppWStr = nullptr;

    switch (plLocalization::GetLanguage()) {
        case plLocalization::kEnglish:
        default:
            *ppStr1 = "Authentication Failed. Please try again.";

            switch (authError) {
                case kNetErrAccountNotFound:
                    *ppStr2 = "Account Not Found.";
                    break;
                case kNetErrAccountNotActivated:
                    *ppStr2 = "Account Not Activated.";
                    break;
                case kNetErrConnectFailed:
                    *ppStr2 = "Unable to connect to Myst Online.";
                    break;
                case kNetErrDisconnected:
                    *ppStr2 = "Disconnected from Myst Online.";
                    break;
                case kNetErrAuthenticationFailed:
                    *ppStr2 = "Incorrect password.\n\nMake sure CAPS LOCK is not on.";
                    break;
                case kNetErrGTServerError:
                case kNetErrGameTapConnectionFailed:
                    *ppStr2 = "Unable to connect to GameTap, please try again in a few minutes.";
                    break;
                case kNetErrAccountBanned:
                    *ppStr2 = "Your account has been banned from accessing Myst Online.  If you are unsure as to why this happened please contact customer support.";
                    break;
                default:
                    *ppWStr = NetErrorToString(authError);
                    break;
            }
            break;
    }
}
