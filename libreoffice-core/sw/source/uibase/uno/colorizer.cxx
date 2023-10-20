/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of Macro's fork of LibreOffice
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 */
// MACRO-1653/MACRO-1598: Colorize and overlays
#include <hintids.hxx>
#include <svl/colorizer.hxx>
#include <svx/swframetypes.hxx>
#include <editeng/colritem.hxx>
#include <IDocumentContentOperations.hxx>
#include <comphelper/servicehelper.hxx>
#include <unotextcursor.hxx>
#include <wrtsh.hxx>
#include <com/sun/star/beans/XMultiPropertySet.hpp>
#include <com/sun/star/beans/XMultiPropertyStates.hpp>
#include <editeng/unoprnms.hxx>
#include <unoprnms.hxx>
#include <com/sun/star/text/XTextViewCursorSupplier.hpp>
#include <com/sun/star/container/XEnumerationAccess.hpp>
#include <com/sun/star/text/XTextTable.hpp>
#include <com/sun/star/text/XTextRange.hpp>
#include <com/sun/star/text/XTextRangeCompare.hpp>
#include <com/sun/star/text/XWordCursor.hpp>
#include <com/sun/star/text/XTextViewCursor.hpp>
#include <com/sun/star/uno/Reference.hxx>
#include <IDocumentTimerAccess.hxx>
#include <LibreOfficeKit/LibreOfficeKitEnums.h>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <UndoManager.hxx>
#include <colorizer.hxx>
#include <docsh.hxx>
#include <memory>
#include <mutex>
#include <atomic>
#include <view.hxx>
#include <viewsh.hxx>

namespace colorizer
{

class CancelFlagSingleton
{
private:
    std::unordered_map<rtl::Reference<SwXTextDocument>, std::atomic<bool>> atomic_map;
    std::mutex mtx;
    CancelFlagSingleton() {}

public:
    static CancelFlagSingleton& getInstance()
    {
        static CancelFlagSingleton instance;
        return instance;
    }

    std::atomic<bool>& getFlag(const rtl::Reference<SwXTextDocument>& doc)
    {
        std::lock_guard<std::mutex> lock(mtx);
        auto it = atomic_map.find(doc);
        if (it == atomic_map.end())
        {
            atomic_map.emplace(doc, false);
            return atomic_map[doc];
        }
        return it->second;
    }

    void cleanup(const rtl::Reference<SwXTextDocument>& doc)
    {
        std::lock_guard<std::mutex> lock(mtx);
        atomic_map.erase(doc);
    }
};

namespace
{
void emitCallback(SwXTextDocument* doc, LibreOfficeKitCallbackType type, const char* event)
{
    if (!doc)
    {
        return;
    }

    // MACRO-1384: prevent crash on load for some comment edgecases
    SwView* pCurView = dynamic_cast<SwView*>(SfxViewShell::Current());
    if (!pCurView)
    {
        return;
    }

    SwDocShell* pDocSh = doc->GetDocShell();
    SwView* pView = pDocSh->GetView();
    if (!pView || pView != pCurView)
    {
        return;
    }

    pView->libreOfficeKitViewCallback(type, event);
}

void emitError(SwXTextDocument* doc, bool forOverlays = false)
{
    emitCallback(doc, forOverlays ? LOK_CALLBACK_MACRO_OVERLAY : LOK_CALLBACK_MACRO_COLORIZER,
                 "{type:\"error\"}");
}

void emitCancelled(SwXTextDocument* doc, bool forOverlays = false)
{
    emitCallback(doc, forOverlays ? LOK_CALLBACK_MACRO_OVERLAY : LOK_CALLBACK_MACRO_COLORIZER,
                 "{type:\"cancelled\"}");
}

void emitFinished(SwXTextDocument* doc, bool forOverlays = false)
{
    emitCallback(doc, forOverlays ? LOK_CALLBACK_MACRO_OVERLAY : LOK_CALLBACK_MACRO_COLORIZER,
                 "{type:\"finished\"}");
}

struct HexData
{
    std::string endHex;
};

struct HexWithRef : HexData
{
    std::string refHex;
};

using HexDataMap = std::unordered_map<std::string, HexData>;
using HexRefMap = std::unordered_map<std::string, HexWithRef>;

struct OverlayData
{
    HexDataMap term;
    HexRefMap termRef;
    HexDataMap anomaly;
};

using HexTextRangeMap = std::unordered_map<std::string, uno::Reference<text::XTextRange>>;
struct AppliedOverlayData
{
    HexTextRangeMap term;
    HexTextRangeMap termRef;
    HexTextRangeMap anomaly;
};

using AppliedOverlayMaps = std::unordered_map<rtl::Reference<SwXTextDocument>, AppliedOverlayData>;

std::unique_ptr<AppliedOverlayMaps>& getAppliedOverlayMaps()
{
    static std::unique_ptr<AppliedOverlayMaps> maps = std::make_unique<AppliedOverlayMaps>();
    return maps;
}

/// word cursor doesn't actually respect the original text range boundary (the paragraph start/end)
/// so the start of the word is compared to the end of the paragraph, if it is after the paragrah
/// then the loop jumps
inline bool isWordBeforeEndOfParagraph(const uno::Reference<text::XTextRangeCompare>& rangeCompare,
                                       const uno::Reference<text::XWordCursor>& wordCursor,
                                       const uno::Reference<text::XTextRange>& paragraphTextRange)
{
    return rangeCompare->compareRegionStarts(wordCursor, paragraphTextRange->getEnd()) == 1;
}

void jumpToOverlay(SwXTextDocument* doc, const std::string& hex, HexTextRangeMap& map)
{
    auto it = map.find(hex);
    if (it == map.end())
    {
        return emitError(doc);
    }

    uno::Reference<text::XTextViewCursorSupplier> xCursorSupplier(doc->getCurrentController(),
                                                                  uno::UNO_QUERY);
    if (!xCursorSupplier)
    {
        return emitError(doc);
    }

    uno::Reference<text::XTextViewCursor> xCursor = xCursorSupplier->getViewCursor();
    doc->resetSelection();
    xCursor->gotoRange(it->second->getStart(), false);
    xCursor->gotoRange(it->second->getEnd(), true);
}

void makeLink(uno::Reference<text::XTextCursor> textCursor, const OUString& link)
{
    static constexpr OUStringLiteral s_UnvisitedCharStyleName = u"Internet Link";
    static constexpr OUStringLiteral s_VisitedCharStyleName = u"Visited Internet Link";

    uno::Reference<beans::XMultiPropertySet> xProps(textCursor, uno::UNO_QUERY);
    if (!xProps)
    {
        return;
    }
    uno::Sequence<OUString> aProperties{ UNO_NAME_HYPER_LINK_U_R_L,
                                         UNO_NAME_VISITED_CHAR_STYLE_NAME,
                                         UNO_NAME_UNVISITED_CHAR_STYLE_NAME };
    uno::Sequence<uno::Any> aValues{ uno::Any(link), uno::Any(s_VisitedCharStyleName),
                                     uno::Any(s_UnvisitedCharStyleName) };
    xProps->setPropertyValues(aProperties, aValues);
}

void unsetLink(uno::Reference<text::XTextRange> range)
{
    uno::Reference<beans::XMultiPropertyStates> xProps(
        range->getText()->createTextCursorByRange(range), uno::UNO_QUERY);
    if (!xProps)
    {
        SAL_WARN("colorizer", "unable to reset applied overlay");
        return;
    }
    uno::Sequence<OUString> aProperties{ UNO_NAME_HYPER_LINK_U_R_L,
                                         UNO_NAME_VISITED_CHAR_STYLE_NAME,
                                         UNO_NAME_UNVISITED_CHAR_STYLE_NAME };
    xProps->setPropertiesToDefault(aProperties);
}

struct OverlayReferenceHex
{
    std::string start;
    std::string end;
    std::string term;
};

struct OverlayState
{
    OverlayData* overlay;
    AppliedOverlayData* appliedOverlay;

    uno::Reference<text::XTextRange> defTextRange;
    std::string defEndHex;
    HexDataMap::const_iterator def; // start, end
    int defCounter;

    uno::Reference<text::XTextRange> refTextRange;
    std::string refEndHex;
    HexRefMap::const_iterator ref; // start, end
    int refCounter;

    uno::Reference<text::XTextRange> anomalyTextRange;
    HexDataMap::const_iterator anomaly; // start, end
    std::string anomalyEndHex;
    int anomalyCounter;
};

// returns true if finished
bool visitWords(SwXTextDocument* doc, OverlayState* overlayState,
                void (*callback)(SwDoc* doc, sal_Int32 color, uno::Reference<text::XWordCursor> word,
                                 OverlayState* overlayState))
{
    uno::Reference<container::XEnumerationAccess> xParaAcess(doc->getText(), uno::UNO_QUERY);
    if (!xParaAcess.is())
    {
        emitError(doc, overlayState);
        return false;
    }

    uno::Reference<container::XEnumeration> xParaIter = xParaAcess->createEnumeration();

    auto& cancelled = CancelFlagSingleton::getInstance().getFlag(doc);

    sal_Int32 nColor = 0x0;

    SwDoc* pDoc = doc->GetDocShell()->GetDoc();

    while (!cancelled && xParaIter->hasMoreElements())
    {
        uno::Any el = xParaIter->nextElement();
        uno::Reference<text::XTextTable> xTable(el, uno::UNO_QUERY);
        if (xTable.is())
        {
            // visiting tables crashes, probably due to differing range text, revisit later
            continue;
        }
        uno::Reference<text::XTextRange> xParaTextRange(el, uno::UNO_QUERY);
        if (!xParaTextRange.is())
        {
            continue;
        }

        uno::Reference<text::XWordCursor> xWordCursor(
            xParaTextRange->getText()->createTextCursorByRange(xParaTextRange), uno::UNO_QUERY);
        uno::Reference<text::XTextRangeCompare> xRangeCompare(xParaTextRange->getText(),
                                                              uno::UNO_QUERY);
        if (!xWordCursor.is() || !xRangeCompare.is())
        {
            continue;
        }

        do
        {
            // select the word
            xWordCursor->gotoStartOfWord(false);
            xWordCursor->gotoEndOfWord(true);

            callback(pDoc, nColor++, xWordCursor, overlayState);
            xWordCursor->gotoNextWord(false);
        } while (!cancelled
                 && isWordBeforeEndOfParagraph(xRangeCompare, xWordCursor, xParaTextRange));
    }
    return !cancelled;
}

// TODO: a lot of repeated code, will refactor at some point
void applyOverlays(SwDoc* /*doc*/, sal_Int32 color, uno::Reference<text::XWordCursor> cursor, OverlayState* state)
{
    const OverlayData& overlay = *state->overlay;
    AppliedOverlayData& appliedOverlay = *state->appliedOverlay;

    std::stringstream ss;
    ss << std::hex << std::setfill('0') << std::setw(6) << sal_uInt32(color & 0x00FFFFFF);
    std::string hex = ss.str();

    auto termIt = overlay.term.find(hex);
    if (termIt != overlay.term.end() && state->defEndHex.empty())
    {
        state->defCounter++;
        state->defTextRange = cursor->getStart();
        state->def = termIt;
        state->defEndHex = termIt->second.endHex;
    }

    if (state->defEndHex == hex)
    {
        if (!state->defTextRange.is() || state->def == overlay.term.end())
        {
            SAL_WARN("colorizer", "end of definition without all necessary variables");
            return;
        }

        state->defCounter--;

        // create new text cursor to not mess up iterator
        uno::Reference<text::XTextCursor> defCursor = cursor->getText()->createTextCursor();
        defCursor->gotoRange(state->defTextRange, false);
        defCursor->gotoRange(cursor->getEnd(), true);

        std::string defStartHex = state->def->first;
        makeLink(defCursor, u"term://" + OUString::createFromAscii(defStartHex));

        uno::Reference<text::XTextRange> textRange(defCursor, uno::UNO_QUERY);
        if (!textRange)
            return;

        appliedOverlay.term[defStartHex] = textRange;

        // reset
        state->defTextRange.clear();
        state->def = overlay.term.end();
        state->defEndHex.clear();
    }

    auto termRefIt = overlay.termRef.find(hex);
    if (termRefIt != overlay.termRef.end() && state->refEndHex.empty())
    {
        state->refCounter++;
        state->refTextRange = cursor->getStart();
        state->ref = termRefIt;
        state->refEndHex = termRefIt->second.endHex;
    }

    if (state->refEndHex == hex)
    {
        if (!state->refTextRange.is() || state->ref == overlay.termRef.end())
        {
            SAL_WARN("colorizer", "end of reference without all necessary variables");
            return;
        }

        state->refCounter--;

        // create new text cursor to not mess up iterator
        uno::Reference<text::XTextCursor> refCursor = cursor->getText()->createTextCursor();
        refCursor->gotoRange(state->refTextRange, false);
        refCursor->gotoRange(cursor->getEnd(), true);

        std::string refStartHex = state->ref->first;
        makeLink(refCursor, u"termref://" + OUString::createFromAscii(refStartHex));

        uno::Reference<text::XTextRange> textRange(refCursor, uno::UNO_QUERY);
        if (!textRange)
            return;

        appliedOverlay.term[refStartHex] = textRange;

        // reset
        state->refTextRange.clear();
        state->ref = overlay.termRef.end();
        state->refEndHex.clear();
    }

    auto anomalyIt = overlay.anomaly.find(hex);
    if (anomalyIt != overlay.anomaly.end() && state->anomalyEndHex.empty())
    {
        state->anomalyCounter++;
        state->anomalyTextRange = cursor->getStart();
        state->anomaly = anomalyIt;
        state->anomalyEndHex = anomalyIt->second.endHex;
    }

    if (state->anomalyEndHex == hex)
    {
        if (!state->anomalyTextRange.is() || state->anomaly == overlay.anomaly.end())
        {
            SAL_WARN("colorizer", "end of anomaly without all necessary variables");
            return;
        }

        state->anomalyCounter--;

        // create new text cursor to not mess up iterator
        uno::Reference<text::XTextCursor> anomalyCursor = cursor->getText()->createTextCursor();
        anomalyCursor->gotoRange(state->anomalyTextRange, false);
        anomalyCursor->gotoRange(cursor->getEnd(), true);

        std::string anomalyStartHex = state->anomaly->first;
        uno::Reference<text::XTextRange> textRange(anomalyCursor, uno::UNO_QUERY);
        if (!textRange)
            return;

        appliedOverlay.term[anomalyStartHex] = textRange;

        // reset
        state->anomalyTextRange.clear();
        state->anomaly = overlay.anomaly.end();
        state->anomalyEndHex.clear();
    }
}

}

void Colorize(SwXTextDocument* doc)
{
    if (!doc)
        return;

    SwDocShell* pDocSh = doc->GetDocShell();
    SwDoc* pDoc = pDocSh->GetDoc();
    pDoc->getIDocumentTimerAccess().BlockIdling();
    pDoc->getIDocumentTimerAccess().StopIdling();
    pDoc->GetUndoManager().DoUndo(false);
    pDocSh->EnableSetModified(false); // don't broadcast attribute changes
    doc->lockControllers();
    doc->setPropertyValue(UNO_NAME_SHOW_CHANGES, uno::Any(false));
    doc->setPropertyValue(UNO_NAME_RECORD_CHANGES, uno::Any(false));

    SetBlockPooling(true);

    bool finished = visitWords(
        doc, nullptr,
        [](SwDoc* pSwDoc, sal_Int32 color, uno::Reference<text::XWordCursor> cursor,
           OverlayState* /*overlayState*/) -> void
        {
            SwXTextCursor* const pInternalCursor
                = comphelper::getFromUnoTunnel<SwXTextCursor>(cursor);
            SvxColorItem sColor(color, RES_CHRATR_COLOR);
            pSwDoc->getIDocumentContentOperations().InsertPoolItem(*pInternalCursor->GetPaM(), sColor);
        });
    // WARN: this is hacky, restores SfxItemPool::PutImpl behavior
    pDoc->GetAttrPool().SetItemInfos(aSlotTab);

    SetBlockPooling(false);

    doc->unlockControllers();

    if (finished)
    {
        emitFinished(doc);
    }
    else
    {
        emitCancelled(doc);
    }
}

void CancelColorize(SwXTextDocument* doc)
{
    CancelFlagSingleton::getInstance().getFlag(doc) = false;
}

void ApplyOverlays(SwXTextDocument* doc, const std::string& json)
{
    if (!doc)
        return;
    SwDocShell* pDocSh = doc->GetDocShell();
    if (!pDocSh)
        return;
    SwWrtShell* pWrtShell = pDocSh->GetWrtShell();
    SwViewShell* pViewShell = pWrtShell;
    if (!pViewShell)
        return emitError(doc, true);

    pWrtShell->StartUndo(SwUndoId::START, nullptr);
    pViewShell->StartAction();
    bool bStateChanged = false;
    if (pDocSh->IsEnableSetModified())
    {
        pDocSh->EnableSetModified(false);
        bStateChanged = true;
    }

    using boost::property_tree::ptree;
    ptree pt;
    std::istringstream jsonStream(json);
    read_json(jsonStream, pt);
    OverlayData payload;

    for (const auto& item : pt.get_child("term"))
    {
        HexData data;
        data.endHex = item.second.get<std::string>("endHex");
        payload.term[item.first] = data;
    }

    for (const auto& item : pt.get_child("termRef"))
    {
        HexWithRef data;
        data.endHex = item.second.get<std::string>("endHex");
        if (item.second.get_optional<std::string>("refHex"))
        {
            data.refHex = item.second.get<std::string>("refHex");
        }
        payload.termRef[item.first] = data;
    }

    for (const auto& item : pt.get_child("anomaly"))
    {
        HexData data;
        data.endHex = item.second.get<std::string>("endHex");
        payload.anomaly[item.first] = data;
    }

    // clear any existing overlays
    ClearOverlays(doc);

    std::unique_ptr<AppliedOverlayMaps>& maps = getAppliedOverlayMaps();

    AppliedOverlayData overlays{};
    OverlayState overlayState{};
    overlayState.overlay = &payload;
    overlayState.appliedOverlay = &overlays;
    overlayState.def = payload.term.end();
    overlayState.ref = payload.termRef.end();

    bool finished = visitWords(doc, &overlayState, applyOverlays);

    if (bStateChanged)
        pDocSh->EnableSetModified();
    pViewShell->EndAction();
    pWrtShell->EndUndo(SwUndoId::END, nullptr);

    maps->emplace(doc, overlays);

    if (overlayState.defCounter > 0)
    {
        SAL_WARN("colorizer", "definition count is off");
    }

    if (overlayState.refCounter > 0)
    {
        SAL_WARN("colorizer", "reference count is off");
    }

    if (overlayState.anomalyCounter > 0)
    {
        SAL_WARN("colorizer", "anomaly count is off");
    }

    if (finished)
    {
        emitFinished(doc, true);
    }
    else
    {
        emitCancelled(doc, true);
    }
}

void ClearOverlays(SwXTextDocument* doc)
{
    if (!doc)
        return;
    SwDocShell* pDocSh = doc->GetDocShell();
    if (!pDocSh)
        return;
    SwWrtShell* pWrtShell = pDocSh->GetWrtShell();
    SwViewShell* pViewShell = pWrtShell;
    if (!pViewShell)
        return emitError(doc, true);
    pWrtShell->StartUndo(SwUndoId::START, nullptr);
    pViewShell->StartAction();

    bool bStateChanged = false;
    if (pDocSh->IsEnableSetModified())
    {
        pDocSh->EnableSetModified(false);
        bStateChanged = true;
    }

    // actually just cancels applying overlays
    CancelColorize(doc);

    auto& maps = getAppliedOverlayMaps();
    auto it = maps->find(doc);
    if (it != maps->end())
    {
        for (const auto& term : it->second.termRef)
        {
            unsetLink(term.second);
        }
        for (const auto& term : it->second.term)
        {
            unsetLink(term.second);
        }
    }

    if (bStateChanged)
        pDocSh->EnableSetModified();

    pViewShell->SwViewShell::UpdateFields(true);
    pViewShell->EndAction();
    pWrtShell->EndUndo(SwUndoId::END, nullptr);

    maps->erase(doc);
}

void JumpToOverlay(SwXTextDocument* doc, const std::string& hex, OverlayType type)
{
    if (!doc)
        return;

    std::unique_ptr<AppliedOverlayMaps>& maps = getAppliedOverlayMaps();
    AppliedOverlayMaps::iterator it = maps->find(doc);
    if (it == maps->end())
    {
        return emitError(doc);
    }

    switch (type)
    {
        case TERM_OVERLAY:
            jumpToOverlay(doc, hex, it->second.term);
            break;
        case TERMREF_OVERLAY:
            jumpToOverlay(doc, hex, it->second.termRef);
            break;
        case ANOMALY_OVERLAY:
            jumpToOverlay(doc, hex, it->second.anomaly);
            break;
    }
}

void Cleanup(SwXTextDocument* doc)
{
    getAppliedOverlayMaps()->erase(doc);
    CancelFlagSingleton::getInstance().cleanup(doc);
}

}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
