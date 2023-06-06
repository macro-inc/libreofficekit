/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
#pragma once
#include "LibreOfficeKit/LibreOfficeKitTypes.h"
#include <config_buildconfig.h>
#include <config_features.h>

#include <stdio.h>
#include <string.h>

#include <com/sun/star/uno/Reference.hxx>
#include <com/sun/star/document/XDocumentEventListener.hpp>
#include <com/sun/star/document/XDocumentEventBroadcaster.hpp>
#include <cppuhelper/basemutex.hxx>
#include <cppuhelper/weakref.hxx>
#include <cppuhelper/implbase.hxx>

namespace doceventnotifier
{
class LokDocumentEventNotifier final
    : private cppu::BaseMutex,
      public cppu::WeakImplHelper<css::document::XDocumentEventListener>
{
public:
    LokDocumentEventNotifier(const css::uno::Reference<css::document::XDocumentEventBroadcaster>& xOwner,
                             const LibreOfficeKitCallback& pCallback, void* pData);

    virtual ~LokDocumentEventNotifier() override;

    void disable();

    /** @see css.document.XDocumentEventListener */
    virtual void SAL_CALL documentEventOccured(const css::document::DocumentEvent& aEvent) override;

    /** @see css.lang.XEventListener */
    virtual void SAL_CALL disposing(const css::lang::EventObject& aEvent) override;

private:
    /** reference to the outside UNO class using this helper. */
    css::uno::Reference<css::document::XDocumentEventBroadcaster> m_xOwner;

    LibreOfficeKitCallback m_pCallback;
    void* m_pData;
    bool m_bDisposed;

}; // LokDocumentEventNotifier

} // doceventnotifier

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
