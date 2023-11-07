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
#include "ndtxt.hxx"
#include <comphelper/threadpool.hxx>
#include <cppuhelper/weakref.hxx>
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
#include <string>
#include <viewsh.hxx>
#include <unordered_map>
#include <unordered_set>

namespace std
{

template <typename T> struct hash<::uno::WeakReference<T>>
{
    std::size_t operator()(::uno::WeakReference<T> const& s) const
    {
        uno::Reference<T> strongRef(s.get());
        return std::size_t(strongRef.is() ? strongRef.get() : nullptr);
    }
};

} // namespace std

namespace colorizer
{

namespace
{

void emitCallback(rtl::Reference<SwXTextDocument> doc, LibreOfficeKitCallbackType type,
                  const char* event)
{
    if (!doc)
    {
        return;
    }

    SwDocShell* pDocSh = doc->GetDocShell();
    SwView* pView = pDocSh->GetView();
    if (!pView)
    {
        return;
    }

    pView->libreOfficeKitViewCallback(type, event);
}

void emitError(rtl::Reference<SwXTextDocument> doc, bool forOverlays = false)
{
    emitCallback(doc, forOverlays ? LOK_CALLBACK_MACRO_OVERLAY : LOK_CALLBACK_MACRO_COLORIZER,
                 "{\"type\":\"error\"}");
}

void emitCancelled(rtl::Reference<SwXTextDocument> doc, bool forOverlays = false)
{
    emitCallback(doc, forOverlays ? LOK_CALLBACK_MACRO_OVERLAY : LOK_CALLBACK_MACRO_COLORIZER,
                 "{\"type\":\"cancelled\"}");
}

void emitFinished(rtl::Reference<SwXTextDocument> doc, bool forOverlays = false)
{
    emitCallback(doc, forOverlays ? LOK_CALLBACK_MACRO_OVERLAY : LOK_CALLBACK_MACRO_COLORIZER,
                 "{\"type\":\"finished\"}");
}

void UpdateTextColorDirectly(SwDoc* pSwDoc, SwPaM* pInternalCursor, const SvxColorItem& color)
{
    if (!pSwDoc || !pInternalCursor) return;

        SwTextNode* pTextNode = pInternalCursor->GetPoint()->nNode.GetNode().GetTextNode();
        if (!pTextNode) return;

            SfxItemSet aSet(pSwDoc->GetAttrPool(), RES_CHRATR_COLOR, RES_CHRATR_COLOR);
            aSet.Put(color);
            const sal_Int32 nStart = pInternalCursor->GetPoint()->nContent.GetIndex();
            const sal_Int32 nEnd = pInternalCursor->GetMark()->nContent.GetIndex();
            pTextNode->SetAttr(aSet, nStart, nEnd);


}

using WeakDocRef = uno::WeakReference<uno::XInterface>;

class WeakDisposer
{
public:
    virtual void dispose(uno::Reference<uno::XInterface> ref) = 0;
};

struct WeakDocumentMapDisposal final : public cppu::WeakImplHelper<lang::XEventListener>
{
    WeakDisposer& m_disposer;
    WeakDocumentMapDisposal(WeakDisposer& disposer)
        : m_disposer(disposer)
    {
    }

    virtual void SAL_CALL disposing(lang::EventObject const& rEvt) override
    {
        m_disposer.dispose(rEvt.Source);
    }
};

template <typename T,
          typename = typename std::enable_if<std::is_default_constructible<T>::value>::type>
class WeakDocumentMap : public std::unordered_map<WeakDocRef, T>, public WeakDisposer
{
private:
    std::mutex mtx;
    using base_map = std::unordered_map<WeakDocRef, T>;
    using base_iterator = typename base_map::iterator;

public:
    std::pair<WeakDocRef, T&> set(uno::Reference<SwXTextDocument> doc, T item)
    {
        uno::WeakReference<uno::XInterface> lookup(doc);
        std::lock_guard<std::mutex> lock(mtx);
        auto res = this->insert_or_assign(lookup, item);
        // this allows automatic cleanup of entries when the source document is disposed
        if (res.second)
        {
            if (doc.is())
            {
                rtl::Reference<WeakDocumentMapDisposal> listener(
                    new WeakDocumentMapDisposal(*this));
                doc->addEventListener(listener);
            }
        }
        base_iterator it = res.first;
        return { it->first, it->second };
    }

    base_iterator find(uno::Reference<SwXTextDocument> doc)
    {
        uno::WeakReference<uno::XInterface> lookup(doc);
        std::lock_guard<std::mutex> lock(mtx);
        return this->base_map::find(lookup);
    }

    void dispose(uno::Reference<uno::XInterface> ref) override
    {
        if (ref.is() && !dynamic_cast<SwXTextDocument*>(ref.get()))
        {
            return;
        }
        uno::WeakReference<uno::XInterface> lookup(ref);
        std::lock_guard<std::mutex> lock(mtx);
        this->erase(lookup);
    }
};

using CancelFlag = std::atomic<bool>;

using CancellableCallback = void (*)(CancelFlag& cancelFlag, rtl::Reference<SwXTextDocument> doc);

class CancellableTask : public comphelper::ThreadTask
{
private:
    CancellableCallback m_func;
    rtl::Reference<SwXTextDocument> m_doc;
    std::atomic<bool>& m_cancelFlag;

public:
    CancellableTask(CancellableCallback func, rtl::Reference<SwXTextDocument> doc,
                    CancelFlag& cancelFlag)
        : comphelper::ThreadTask(comphelper::ThreadPool::createThreadTaskTag())
        , m_func(func)
        , m_doc(doc)
        , m_cancelFlag(cancelFlag)
    {
    }

    virtual void doWork() override { m_func(m_cancelFlag, m_doc); }
};

class CancellableTaskManager
{
private:
    std::unordered_map<std::size_t, CancelFlag> cancelFlags;
    std::mutex mtx;

public:
    void startThread(rtl::Reference<SwXTextDocument> doc, CancellableCallback func)
    {
        std::lock_guard<std::mutex> lock(mtx);
        std::size_t id = std::size_t(doc.get());

        // Cancel existing thread with the same id if any
        auto it = cancelFlags.find(id);
        if (it != cancelFlags.end())
        {
            it->second = true; // Signal cancellation
            cancelFlags.erase(it); // Remove from map
        }

        // Add new cancellation flag
        cancelFlags[id] = false;

        // Create and schedule new task
        comphelper::ThreadPool::getSharedOptimalPool().pushTask(
            std::make_unique<CancellableTask>(func, doc, cancelFlags[id]));
    }

    bool cancelThread(rtl::Reference<SwXTextDocument> doc)
    {
        std::lock_guard<std::mutex> lock(mtx);
        auto it = cancelFlags.find(std::size_t(doc.get()));
        if (it == cancelFlags.end())
        {
            return false;
        }

        it->second = true; // Signal cancellation
        cancelFlags.erase(it); // Remove from map
        return true;
    }
};

class ColorizerTaskManager : CancellableTaskManager
{
private:
    static CancellableTaskManager& getInstance()
    {
        static CancellableTaskManager singleton{};
        return singleton;
    }

public:
    static void start(rtl::Reference<SwXTextDocument> doc, CancellableCallback callback)
    {
        getInstance().startThread(doc, callback);
    }

    static void cancel(rtl::Reference<SwXTextDocument> doc) { getInstance().cancelThread(doc); }
};

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
using HexRefTextRangeMap
    = std::unordered_map<std::string, std::pair<OUString, uno::Reference<text::XTextRange>>>;
struct AppliedOverlayData
{
    HexTextRangeMap term;
    HexRefTextRangeMap termRef;
    HexTextRangeMap anomaly;
};

using AppliedOverlayMaps = WeakDocumentMap<AppliedOverlayData>;

AppliedOverlayMaps& getAppliedOverlayMaps()
{
    static AppliedOverlayMaps maps{};
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

void jumpToOverlay(rtl::Reference<SwXTextDocument> doc, const std::string& hex,
                   HexTextRangeMap& map)
{
    auto it = map.find(hex);
    if (it == map.end())
    {
        return emitError(doc, true);
    }

    uno::Reference<text::XTextViewCursorSupplier> xCursorSupplier(doc->getCurrentController(),
                                                                  uno::UNO_QUERY);
    if (!xCursorSupplier)
    {
        return emitError(doc, true);
    }

    uno::Reference<text::XTextViewCursor> xCursor = xCursorSupplier->getViewCursor();
    doc->resetSelection();
    xCursor->gotoRange(it->second->getStart(), false);
    xCursor->gotoRange(it->second->getEnd(), true);
}

void jumpToOverlay(rtl::Reference<SwXTextDocument> doc, const std::string& hex,
                   HexRefTextRangeMap& map)
{
    auto it = map.find(hex);
    if (it == map.end())
    {
        return emitError(doc, true);
    }

    uno::Reference<text::XTextViewCursorSupplier> xCursorSupplier(doc->getCurrentController(),
                                                                  uno::UNO_QUERY);
    if (!xCursorSupplier)
    {
        return emitError(doc, true);
    }

    uno::Reference<text::XTextViewCursor> xCursor = xCursorSupplier->getViewCursor();
    doc->resetSelection();
    xCursor->gotoRange(it->second.second->getStart(), false);
    xCursor->gotoRange(it->second.second->getEnd(), true);
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
bool visitWords(rtl::Reference<SwXTextDocument> doc, OverlayState* overlayState,
                std::atomic<bool>& cancelled,
                void (*callback)(SwDoc* doc, sal_Int32 color,
                                 uno::Reference<text::XWordCursor> word,
                                 OverlayState* overlayState))
{
    uno::Reference<container::XEnumerationAccess> xParaAcess(doc->getText(), uno::UNO_QUERY);
    if (!xParaAcess.is())
    {
        emitError(doc, overlayState);
        return false;
    }

    uno::Reference<container::XEnumeration> xParaIter = xParaAcess->createEnumeration();

    sal_Int32 nColor = 0x000007; // 0 is the "unset" color and 7 is a lucky number

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

            // color anything in between
            xWordCursor->collapseToEnd();
            xWordCursor->gotoNextWord(true);

            callback(pDoc, nColor++, xWordCursor, overlayState);

            if (isWordBeforeEndOfParagraph(xRangeCompare, xWordCursor, xParaTextRange))
            {
                xWordCursor->goRight(1, false);
            }
        } while (!cancelled
                 && isWordBeforeEndOfParagraph(xRangeCompare, xWordCursor, xParaTextRange));
    }
    return !cancelled;
}

// TODO: a lot of repeated code, will refactor at some point
void applyOverlays(SwDoc* /*doc*/, sal_Int32 color, uno::Reference<text::XWordCursor> cursor,
                   OverlayState* state)
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
        OUString refTargetHex = OUString::createFromAscii(state->ref->second.refHex);
        makeLink(refCursor, u"termref://" + refTargetHex);

        uno::Reference<text::XTextRange> textRange(refCursor, uno::UNO_QUERY);
        if (!textRange)
            return;

        appliedOverlay.termRef[refStartHex] = std::make_pair(refTargetHex, textRange);

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

        appliedOverlay.anomaly[anomalyStartHex] = textRange;

        // reset
        state->anomalyTextRange.clear();
        state->anomaly = overlay.anomaly.end();
        state->anomalyEndHex.clear();
    }
}

// this makes sure that all text we can touch is 0x000000 hidden by default
// anything thats difficult to modify is hidden
void resetColors(CancelFlag& cancelFlag, rtl::Reference<SwXTextDocument> doc)
{
    SwDocShell* pDocSh = doc->GetDocShell();
    SwDoc* pDoc = pDocSh->GetDoc();

    uno::Reference<text::XTextCursor> xBodyCursor = doc->getText()->createTextCursor();
    xBodyCursor->gotoStart(false);
    xBodyCursor->gotoEnd(true);

    SwXTextCursor* pInternalCursor = comphelper::getFromUnoTunnel<SwXTextCursor>(xBodyCursor);
    SvxColorItem sResetColor(0, RES_CHRATR_COLOR);
    UpdateTextColorDirectly(pDoc, pInternalCursor->GetPaM(), sResetColor);

    uno::Reference<container::XIndexAccess> xSections(doc->getTextSections(), uno::UNO_QUERY);

    if (xSections.is())
    {
        sal_Int32 nCount = xSections->getCount();
        for (sal_Int32 i = 0; !cancelFlag && i < nCount; i++)
        {
            if (cancelFlag)
                break;

            css::uno::Reference<css::text::XTextSection> xSection;
            xSections->getByIndex(i) >>= xSection;

            css::uno::Reference<css::text::XText> xText = xSection->getAnchor()->getText();
            uno::Reference<text::XTextCursor> xSectionCursor
                = xSection->getAnchor()->getText()->createTextCursor();
            xSectionCursor->gotoRange(xSection->getAnchor(), true);
            pInternalCursor = comphelper::getFromUnoTunnel<SwXTextCursor>(xBodyCursor);
            if (!pInternalCursor)
                continue;
            UpdateTextColorDirectly(pDoc, pInternalCursor->GetPaM(), sResetColor);
        }
    }

    // instead of resetting the colors in the headers/footers, just turn them off
    uno::Reference<container::XIndexAccess> xPageStyles(
        doc->getStyleFamilies()->getByName("PageStyles"), css::uno::UNO_QUERY);
    if (xPageStyles.is())
    {
        sal_Int32 nStyles = xPageStyles->getCount();
        for (sal_Int32 i = 0; !cancelFlag && i < nStyles; ++i)
        {
            css::uno::Reference<css::beans::XPropertySet> xPropertySet;
            xPageStyles->getByIndex(i) >>= xPropertySet;
            if (xPropertySet.is()) {
                xPropertySet->setPropertyValue("HeaderIsOn", uno::Any(false));
                xPropertySet->setPropertyValue("FooterIsOn", uno::Any(false));
            }
        }
    }

    // TODO: colorize tables?
    uno::Reference<container::XIndexAccess> xTables(doc->getTextTables(), uno::UNO_QUERY);
    if (!xTables.is())
        return;
    sal_Int32 nTables = xTables->getCount();

    for (sal_Int32 i = 0; !cancelFlag && i < nTables; ++i)
    {
        css::uno::Reference<css::text::XTextTable> xTable;
        xTables->getByIndex(i) >>= xTable;
        if (!xTable.is())
            continue;

        css::uno::Sequence<OUString> cellNames = xTable->getCellNames();
        for (const auto& cellName : cellNames)
        {
            if (cancelFlag)
                break;

            css::uno::Reference<css::table::XCell> xCell = xTable->getCellByName(cellName);
            css::uno::Reference<css::text::XText> xCellText(xCell, css::uno::UNO_QUERY);
            if (!xCellText.is())
                continue;
            uno::Reference<text::XTextCursor> xTableCursor = xCellText->createTextCursor();
            xTableCursor->gotoStart(false);
            xTableCursor->gotoEnd(true);
            pInternalCursor = comphelper::getFromUnoTunnel<SwXTextCursor>(xBodyCursor);
            if (!pInternalCursor)
                continue;
            UpdateTextColorDirectly(pDoc, pInternalCursor->GetPaM(), sResetColor);
        }
    }
}




void colorize(CancelFlag& cancelFlag, rtl::Reference<SwXTextDocument> doc)
{

    // TODO: Make thread safe? SolarMutexGuard locks out the cancel flag, so this isn't simple

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
    resetColors(cancelFlag, doc);

    bool finished
        = visitWords(doc, nullptr, cancelFlag,
                     [](SwDoc* pSwDoc, sal_Int32 color, uno::Reference<text::XWordCursor> cursor,
                        OverlayState* /*overlayState*/) -> void
                     {
                         unsetLink(cursor);
                         SwXTextCursor* const pInternalCursor
                             = comphelper::getFromUnoTunnel<SwXTextCursor>(cursor);
                         SvxColorItem sColor(color, RES_CHRATR_COLOR);
                         UpdateTextColorDirectly(pSwDoc, pInternalCursor->GetPaM(), sColor);
                     });

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

void reapplyOverlays(rtl::Reference<SwXTextDocument> doc)
{
    SolarMutexGuard aGuard;
    AppliedOverlayMaps& maps = getAppliedOverlayMaps();
    auto it = maps.find(doc);
    if (it != maps.end())
    {
        for (const auto& termref : it->second.termRef)
        {
            uno::Reference<text::XTextCursor> defCursor
                = termref.second.second->getText()->createTextCursorByRange(termref.second.second);
            makeLink(defCursor, u"termref://" + termref.second.first);
        }

        for (const auto& term : it->second.term)
        {
            uno::Reference<text::XTextCursor> defCursor
                = term.second->getText()->createTextCursorByRange(term.second);
            makeLink(defCursor, u"term://" + OUString::fromUtf8(term.first));
        }
    }
}

void clearOverlays(rtl::Reference<SwXTextDocument> doc, std::set<OverlayType> skipTypes,
                   bool keepRanges)
{
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

    AppliedOverlayMaps& maps = getAppliedOverlayMaps();
    auto it = maps.find(doc);
    if (it != maps.end())
    {
        if (skipTypes.find(OverlayType::TERMREF_OVERLAY) == skipTypes.end())
        {
            for (const auto& termref : it->second.termRef)
            {
                unsetLink(termref.second.second);
            }
            if (!keepRanges)
            {
                it->second.termRef.clear();
            }
        }

        if (skipTypes.find(OverlayType::TERM_OVERLAY) == skipTypes.end())
        {
            for (const auto& term : it->second.term)
            {
                unsetLink(term.second);
            }
            if (!keepRanges)
            {
                it->second.term.clear();
            }
        }

        if (skipTypes.find(OverlayType::ANOMALY_OVERLAY) == skipTypes.end() && !keepRanges)
        {
            it->second.anomaly.clear();
        }
    }

    if (bStateChanged)
        pDocSh->EnableSetModified();

    pViewShell->SwViewShell::UpdateFields(true);
    pViewShell->EndAction();
    pWrtShell->EndUndo(SwUndoId::END, nullptr);
}

} // namespace

void Colorize(rtl::Reference<SwXTextDocument> doc) { ColorizerTaskManager::start(doc, colorize); }

void CancelColorize(rtl::Reference<SwXTextDocument> doc) { ColorizerTaskManager::cancel(doc); }

void ApplyOverlays(rtl::Reference<SwXTextDocument> doc, const char* json)
{
    SolarMutexGuard aGuard;

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

    // simply re-apply link styling for existing ranges
    if (!json)
    {
        reapplyOverlays(doc);

        if (bStateChanged)
        {
            pDocSh->EnableSetModified();
        }
        pViewShell->EndAction();
        pWrtShell->EndUndo(SwUndoId::END, nullptr);

        emitFinished(doc, true);
        return;
    }

    using boost::property_tree::ptree;
    ptree pt;
    std::istringstream jsonStream(json);
    read_json(jsonStream, pt);
    OverlayData payload;
    std::set<OverlayType> skippedTypes;

    auto terms = pt.get_child_optional("term");
    if (terms)
    {
        for (const auto& item : *terms)
        {
            HexData data;
            data.endHex = item.second.get<std::string>("endHex");
            payload.term[item.first] = data;
        }
    }
    else
    {
        skippedTypes.emplace(OverlayType::TERM_OVERLAY);
    }

    auto termrefs = pt.get_child_optional("termRef");
    if (termrefs)
    {
        for (const auto& item : *termrefs)
        {
            HexWithRef data;
            data.endHex = item.second.get<std::string>("endHex");
            if (item.second.get_optional<std::string>("refHex"))
            {
                data.refHex = item.second.get<std::string>("refHex");
            }
            payload.termRef[item.first] = data;
        }
    }
    else
    {
        skippedTypes.emplace(OverlayType::TERMREF_OVERLAY);
    }

    auto anomalies = pt.get_child_optional("anomaly");
    if (anomalies)
    {
        for (const auto& item : *anomalies)
        {
            HexData data;
            data.endHex = item.second.get<std::string>("endHex");
            payload.anomaly[item.first] = data;
        }
    }
    else
    {
        skippedTypes.emplace(OverlayType::ANOMALY_OVERLAY);
    }

    // clear any existing overlays
    clearOverlays(doc, skippedTypes, false);

    AppliedOverlayMaps& maps = getAppliedOverlayMaps();

    AppliedOverlayData overlays{};
    OverlayState overlayState{};
    overlayState.overlay = &payload;
    overlayState.appliedOverlay = &overlays;
    overlayState.def = payload.term.end();
    overlayState.ref = payload.termRef.end();
    CancelFlag dummy_flag;

    bool finished = visitWords(doc, &overlayState, dummy_flag, applyOverlays);

    if (bStateChanged)
    {
        pDocSh->EnableSetModified();
    }
    pViewShell->EndAction();
    pWrtShell->EndUndo(SwUndoId::END, nullptr);

    maps.set(doc, overlays);

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

void ClearOverlays(rtl::Reference<SwXTextDocument> doc, const char* json)
{
    SolarMutexGuard aGuard;
    if (!doc)
        return;

    if (json == nullptr)
    {
        return clearOverlays(doc, {}, false);
    }

    using boost::property_tree::ptree;
    ptree pt;
    std::istringstream jsonStream(json);
    read_json(jsonStream, pt);

    std::set<OverlayType> skippedTypes;

    auto skip = pt.get_child_optional("skip");
    if (skip)
    {
        for (const auto& item : *skip)
        {
            auto maybeType = item.second.get_value<sal_Int32>();
            if (maybeType > OverlayType::START && maybeType < OverlayType::END)
            {
                skippedTypes.emplace((OverlayType)maybeType);
            }
        }
    }

    bool keepRanges = pt.get<bool>("keepRanges", false);
    clearOverlays(doc, skippedTypes, keepRanges);
}

void JumpToOverlay(rtl::Reference<SwXTextDocument> doc, const char* json)
{
    SolarMutexGuard aGuard;
    if (!doc || !json)
        return;

    AppliedOverlayMaps& maps = getAppliedOverlayMaps();
    AppliedOverlayMaps::iterator it = maps.find(doc);
    if (it == maps.end())
    {
        return emitError(doc, true);
    }

    using boost::property_tree::ptree;
    ptree pt;
    std::istringstream jsonStream(json);
    read_json(jsonStream, pt);
    std::string hex = pt.get<std::string>("hex");
    OverlayType type = (OverlayType)pt.get<sal_Int32>("type");

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
        case START:
        case END:
            break;
    }
}

} // namespace colorizer

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
