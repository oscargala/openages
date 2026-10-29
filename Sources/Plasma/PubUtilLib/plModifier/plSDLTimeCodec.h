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
// The SDL time-codec test seam: where a harness build's ATC epoch pin and its ATC trace lines
// (the private test harness's) meet the animation SDL. The product build (PLASMA_TEST_HOOKS OFF) has none of
// it: this header declares nothing there, and every call site is under #ifdef PLASMA_TEST_HOOKS, so the product
// converts an animation's TIME variables as upstream does (plSimpleStateVariable::Get and Set of a double). In a
// harness build the pointer is null unless the client seam installs the codec, which it does only when the test harness
// asks for the pin or the trace; with the pointer null every call site does what the product does.
//
// The call sites:
// - plAnimTimeConvertSDLModifier::IGetTime and IPutTime, the TIME variables of an AnimTimeConvert record (Layer and
//   AGMaster alike), and ISetCurrentATC, once a record is applied;
// - plNetClientMsgHandler's plNetMsgSDLState handler, a received record;
// - plLayerSDLModifier::ISetCurrentStateFrom, a layer a record was applied to, and plLayerAnimation::Eval and
//   plAnimTimeConvert::WorldToAnimTime, that layer's next converter evaluation.
// Everything runs on the client thread.
#ifndef plSDLTimeCodec_inc
#define plSDLTimeCodec_inc

#ifdef PLASMA_TEST_HOOKS

#include <string_theory/string>

class plAnimTimeConvert;
class plATCEaseCurve;
class plLayerAnimation;
class plSDLModifier;
class plSimpleStateVariable;
class plStateDataRecord;
class plUoid;

class plSDLTimeCodec
{
public:
    virtual ~plSDLTimeCodec() = default;

    // IGetTime: a received TIME variable as game time. Returns false to leave it to the product's conversion.
    virtual bool GetTime(const plSimpleStateVariable* var, double* game) { return false; }

    // IPutTime: game time into a TIME variable that will be sent. Returns false to leave it to the product's
    // conversion.
    virtual bool PutTime(plSimpleStateVariable* var, double game) { return false; }

    // ISetCurrentATC, after a used AnimTimeConvert record was applied to atc: the record, the converter's ease curve
    // and the two values the record started its latest state with.
    virtual void OnApply(const plSDLModifier* modifier, const plStateDataRecord* record, plAnimTimeConvert* atc,
                         const plATCEaseCurve* ease, float lastStateAnimTime, double lastStateChange) {}

    // The plNetMsgSDLState handler, once a record of the descriptor arrived for owner: the record as the wire carries
    // it and the record that will be delivered (the same one, but for an initial state, which is merged into the
    // descriptor's defaults first). arrival is the initial-state count before an initial record, -1 for any other.
    virtual void OnReceive(const ST::string& descriptor, const plUoid& owner, bool initial, int arrival,
                           const plStateDataRecord* wire, const plStateDataRecord* delivered) {}

    // plLayerSDLModifier::ISetCurrentStateFrom: a record was applied to layer.
    virtual void OnLayerApplied(plLayerAnimation* layer) {}

    // plLayerAnimation::Eval, around its converter's WorldToAnimTime(wSecs), which returned secs. AfterLayerEval
    // follows only a BeforeLayerEval that returned true.
    virtual bool BeforeLayerEval(plLayerAnimation* layer) { return false; }
    virtual void AfterLayerEval(plLayerAnimation* layer, double wSecs, float secs) {}

    // plAnimTimeConvert::WorldToAnimTime, when it advances the anim time: the delSecs it adds to the current anim time,
    // after the kBackwards negation.
    virtual void OnAnimTimeStep(const plAnimTimeConvert* atc, float delSecs) {}

    static plSDLTimeCodec* Get() { return fCodec; }
    static void            Set(plSDLTimeCodec* codec) { fCodec = codec; }

private:
    static inline plSDLTimeCodec* fCodec = nullptr;
};

#endif // PLASMA_TEST_HOOKS

#endif // plSDLTimeCodec_inc
