#include "lokdocumenteventnotifier.hxx"
#include <com/sun/star/document/XDocumentEventListener.hpp>
#include "LibreOfficeKit/LibreOfficeKitEnums.h"
#include "sal/log.hxx"
#include "vcl/svapp.hxx"

namespace doceventnotifier
{

LokDocumentEventNotifier::LokDocumentEventNotifier(
    const css::uno::Reference<css::document::XDocumentEventBroadcaster>& xOwner,
    const LibreOfficeKitCallback& pCallback, void* pData)
    : cppu::BaseMutex()
    , m_xOwner(xOwner)
    , m_pCallback(pCallback)
    , m_pData(pData)
    , m_bDisposed(false)
{
    xOwner->addDocumentEventListener(static_cast<css::document::XDocumentEventListener*>(this));
}

LokDocumentEventNotifier::~LokDocumentEventNotifier()
{
}

void LokDocumentEventNotifier::disable() {
    if (m_bDisposed)
        return;
    m_bDisposed = true;
}

void SAL_CALL LokDocumentEventNotifier::disposing(const css::lang::EventObject& aEvent)
{
    if (m_bDisposed)
        return;
    SAL_WARN("lokdocumenteventnotifier", "Disposing");
    m_bDisposed = true;

    if (m_xOwner.is() && aEvent.Source == m_xOwner) m_xOwner.clear();
}

void SAL_CALL
LokDocumentEventNotifier::documentEventOccured(const css::document::DocumentEvent& aEvent)
{
    if (m_bDisposed || m_pCallback == nullptr || m_pData == nullptr)
        return;

    if (!aEvent.EventName.equalsIgnoreAsciiCase("OnNew")
        && !aEvent.EventName.equalsIgnoreAsciiCase("OnLoad")
        && !aEvent.EventName.equalsIgnoreAsciiCase("OnSave")
        && !aEvent.EventName.equalsIgnoreAsciiCase("OnSaveDone")
        && !aEvent.EventName.equalsIgnoreAsciiCase("OnSaveAs")
        && !aEvent.EventName.equalsIgnoreAsciiCase("OnSaveAsDone")
        && !aEvent.EventName.equalsIgnoreAsciiCase("OnUnload")
        && !aEvent.EventName.equalsIgnoreAsciiCase("OnTitleChanged")
        && !aEvent.EventName.equalsIgnoreAsciiCase("OnModeChanged"))
    {
        // Uncomment these if you want to see more events
        /* SAL_WARN("lokdocumenteventnotifier", "Unhandled event"); */
        /* SAL_WARN("lokdocumenteventnotifier", aEvent.EventName); */
        return;
    }

    if (!m_xOwner.is() || aEvent.Source != m_xOwner)
    {
        SAL_WARN("lokdocumenteventnotifier", "Owner does not match event source");
        return;
    }

    if (aEvent.EventName.equalsIgnoreAsciiCase("OnNew"))
    {
        m_pCallback(LOK_DOC_CALLBACK_ON_NEW, nullptr, m_pData);
    }
    else if (aEvent.EventName.equalsIgnoreAsciiCase("OnLoad"))
    {
        m_pCallback(LOK_DOC_CALLBACK_ON_LOAD, nullptr, m_pData);
    }
    else if (aEvent.EventName.equalsIgnoreAsciiCase("OnSave"))
    {
        m_pCallback(LOK_DOC_CALLBACK_ON_SAVE, nullptr, m_pData);
    }
    else if (aEvent.EventName.equalsIgnoreAsciiCase("OnSaveDone"))
    {
        m_pCallback(LOK_DOC_CALLBACK_ON_SAVE_DONE, nullptr, m_pData);
    }
    else if (aEvent.EventName.equalsIgnoreAsciiCase("OnSaveAs"))
    {
        m_pCallback(LOK_DOC_CALLBACK_ON_SAVE_AS, nullptr, m_pData);
    }
    else if (aEvent.EventName.equalsIgnoreAsciiCase("OnSaveAsDone"))
    {
        m_pCallback(LOK_DOC_CALLBACK_ON_SAVE_AS_DONE, nullptr, m_pData);
    }
    else if (aEvent.EventName.equalsIgnoreAsciiCase("OnTitleChanged"))
    {
        m_pCallback(LOK_DOC_CALLBACK_ON_TITLE_CHANGED, nullptr, m_pData);
    }
    else if (aEvent.EventName.equalsIgnoreAsciiCase("OnModeChanged"))
    {
        m_pCallback(LOK_DOC_CALLBACK_ON_MODE_CHANGED, nullptr, m_pData);
    }
}

}
