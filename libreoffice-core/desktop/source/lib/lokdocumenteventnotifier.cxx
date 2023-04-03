#include "lokdocumenteventnotifier.hxx"
#include <com/sun/star/document/XDocumentEventBroadcaster.hpp>
#include <com/sun/star/document/XDocumentEventListener.hpp>
#include "LibreOfficeKit/LibreOfficeKitEnums.h"
#include "sal/log.hxx"

#include <com/sun/star/frame/XModel3.hpp>
#include <osl/mutex.hxx>

doceventnotifier::LokDocumentEventNotifier::LokDocumentEventNotifier(
    const css::uno::Reference<css::lang::XComponent>& rxContext,
    const css::uno::Reference<css::uno::XInterface>& xOwner,
    const LibreOfficeKitCallback& pCallback, void* pData)
    : ::cppu::BaseMutex()
    , m_xContext(rxContext)
    , m_pCallback(pCallback)
    , m_pData(pData)
{
    // SYNCHRONIZED ->
    {
        osl::MutexGuard aLock(m_aMutex);
        m_xOwner = xOwner;
    }
    // <- SYNCHRONIZED

    css::uno::Reference<css::frame::XModel> xModel(xOwner, css::uno::UNO_QUERY);
    if (xModel.is())
    {
        SAL_WARN("lokdocumenteventnotifier", "impl_startListeningForModel");
        impl_startListeningForModel(xModel);
        return;
    }
}

void doceventnotifier::LokDocumentEventNotifier::setCallbacks(
    const LibreOfficeKitCallback& pCallback, void* pData)
{
    this->m_pCallback = pCallback;
    this->m_pData = pData;
}

doceventnotifier::LokDocumentEventNotifier::~LokDocumentEventNotifier(){}

void SAL_CALL
doceventnotifier::LokDocumentEventNotifier::disposing(const css::lang::EventObject& aEvent)
{
    css::uno::Reference<css::uno::XInterface> xOwner;

    // SYNCHRONIZED ->
    {
        osl::MutexGuard aLock(m_aMutex);

        xOwner = m_xOwner;
    }

    if (!xOwner.is())
        return;

    if (xOwner != aEvent.Source)
        return;

    {
        osl::MutexGuard aLock(m_aMutex);

        m_xOwner = nullptr;
        m_pCallback = nullptr;
        m_pData = nullptr;
    }
}

void doceventnotifier::LokDocumentEventNotifier::impl_startListeningForModel(
    const css::uno::Reference<css::frame::XModel>& xModel)
{
    css::uno::Reference<css::document::XDocumentEventBroadcaster> xBroadcaster(xModel,
                                                                               css::uno::UNO_QUERY);

    if (!xBroadcaster.is())
        return;

    xBroadcaster->addDocumentEventListener(
        static_cast<css::document::XDocumentEventListener*>(this));
}

void SAL_CALL doceventnotifier::LokDocumentEventNotifier::documentEventOccured(
    const css::document::DocumentEvent& aEvent)
{
    if (m_pCallback == nullptr || m_pData == nullptr)
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

    css::uno::Reference<css::frame::XModel> xOwner;
    // SYNCHRONIZED ->
    {
        osl::MutexGuard aLock(m_aMutex);

        xOwner.set(m_xOwner.get(), css::uno::UNO_QUERY);
    }
    // <- SYNCHRONIZED

    if (aEvent.Source != xOwner && !xOwner.is())
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
