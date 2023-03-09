/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
#pragma once
#include "LibreOfficeKit/LibreOfficeKitTypes.h"
#include <config_buildconfig.h>
#include <config_features.h>

#include <stdio.h>
#include <string.h>

#include <com/sun/star/uno/Reference.hxx>
#include <com/sun/star/document/XDocumentEventListener.hpp>
#include <cppuhelper/basemutex.hxx>
#include <cppuhelper/weakref.hxx>
#include <cppuhelper/implbase.hxx>
#include <framework/fwkdllapi.h>

using namespace css;
using namespace css::uno;
using namespace css::frame;

namespace com { namespace sun { namespace star { namespace frame { class XController; } } } }
namespace com { namespace sun { namespace star { namespace uno { class XComponentContext; } } } }
namespace com { namespace sun { namespace star { namespace uno { class XComponentContext; } } } }
namespace com { namespace sun { namespace star { namespace frame { class XModel3; } } } }
namespace com { namespace sun { namespace star { namespace uno { class XInterface; } } } }

namespace doceventnotifier
{
class FWK_DLLPUBLIC LokDocumentEventNotifier final
    : private ::cppu::BaseMutex,
      public ::cppu::WeakImplHelper<css::document::XDocumentEventListener>
{
public:
    LokDocumentEventNotifier(const css::uno::Reference<css::lang::XComponent>& rxContext,
                             const css::uno::Reference<css::uno::XInterface>& xOwner,
                             const LibreOfficeKitCallback& pCallback, void* pData);

    void setCallbacks(const LibreOfficeKitCallback& pCallback, void* pData);

    virtual ~LokDocumentEventNotifier() override;
    /** @see css.document.XDocumentEventListener */
    virtual void SAL_CALL documentEventOccured(const css::document::DocumentEvent& aEvent) override;

    /** @see css.lang.XEventListener */
    virtual void SAL_CALL disposing(const css::lang::EventObject& aEvent) override;

    // internal
private:
    void impl_startListeningForModel(const css::uno::Reference<css::frame::XModel>& xModel);

    // member
private:
    /** points to the global uno service manager. */
    css::uno::Reference<css::lang::XComponent> m_xContext;

    /** reference to the outside UNO class using this helper. */
    css::uno::WeakReference<css::uno::XInterface> m_xOwner;

    LibreOfficeKitCallback m_pCallback;
    void* m_pData;

}; // LokDocumentEventNotifier

} // doceventnotifier

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
