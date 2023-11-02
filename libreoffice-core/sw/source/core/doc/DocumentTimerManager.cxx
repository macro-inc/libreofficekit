/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * This file incorporates work covered by the following license notice:
 *
 *   Licensed to the Apache Software Foundation (ASF) under one or more
 *   contributor license agreements. See the NOTICE file distributed
 *   with this work for additional information regarding copyright
 *   ownership. The ASF licenses this file to you under the Apache
 *   License, Version 2.0 (the "License"); you may not use this file
 *   except in compliance with the License. You may obtain a copy of
 *   the License at http://www.apache.org/licenses/LICENSE-2.0 .
 */
#include "comphelper/diagnose_ex.hxx"
#include <DocumentTimerManager.hxx>

#include <doc.hxx>
#include <DocumentSettingManager.hxx>
#include <IDocumentFieldsAccess.hxx>
#include <IDocumentLayoutAccess.hxx>
#include <rootfrm.hxx>
#include <viewsh.hxx>
#include <unotools/lingucfg.hxx>
#include <unotools/linguprops.hxx>
#include <fldupde.hxx>
#include <sfx2/progress.hxx>
#include <viewopt.hxx>
#include <docsh.hxx>
#include <docfld.hxx>
#include <fldbas.hxx>
#include <vcl/scheduler.hxx>
#include <comphelper/lok.hxx>
#include <editsh.hxx>
#include <com/sun/star/frame/XStorable.hpp>
#include <com/sun/star/uno/Reference.hxx>
#include <unotools/mediadescriptor.hxx>

namespace sw
{

namespace {
constexpr sal_uInt64 kDebounceRateMilliseconds = 350;
}

DocumentTimerManager::DocumentTimerManager(SwDoc& i_rSwdoc)
    : m_rDoc(i_rSwdoc)
    , m_nIdleBlockCount(0)
    , m_bStartOnUnblock(false)
    , m_aDocIdle(i_rSwdoc, "sw::DocumentTimerManager m_aDocIdle")
    , m_bWaitForLokInit(true)
    , m_sBackupPath()
    , m_aLastBackup()
{
    m_aDocIdle.SetPriority(TaskPriority::LOWEST);
    m_aDocIdle.SetInvokeHandler(LINK(this, DocumentTimerManager, DoIdleJobs));
}

void DocumentTimerManager::StartIdling()
{
    if (m_bWaitForLokInit && comphelper::LibreOfficeKit::isActive())
    {
        StopIdling();
        return;
    }

    m_bWaitForLokInit = false;
    m_bStartOnUnblock = true;
    // debounce
    if (0 == m_nIdleBlockCount)
    {
        m_aDocIdle.Start();
    }
}

void DocumentTimerManager::StopIdling()
{
    // SAL_INFO("lok.load","STOP IDLE");
    m_bStartOnUnblock = false;
    m_aDocIdle.Stop();
    m_aDocIdle.SetTimeout(kDebounceRateMilliseconds);
}

void DocumentTimerManager::BlockIdling()
{
    // SAL_INFO("lok.load","BLOCK IDLE");
    assert(SAL_MAX_UINT32 != m_nIdleBlockCount);
    osl_atomic_increment(&m_nIdleBlockCount);
}

void DocumentTimerManager::UnblockIdling()
{
    // SAL_INFO("lok.load","UNBLOCK IDLE");
    assert(0 != m_nIdleBlockCount);

    if (0 == osl_atomic_decrement(&m_nIdleBlockCount) && m_bStartOnUnblock)
    {
        m_aDocIdle.Start();
    }
}

DocumentTimerManager::IdleJob DocumentTimerManager::GetNextIdleJob() const
{
    SwRootFrame* pTmpRoot = m_rDoc.getIDocumentLayoutAccess().GetCurrentLayout();

    if( pTmpRoot &&
        !SfxProgress::GetActiveProgress( m_rDoc.GetDocShell() ) )
    {
        SwViewShell* pShell(m_rDoc.getIDocumentLayoutAccess().GetCurrentViewShell());
        for(const SwViewShell& rSh : pShell->GetRingContainer())
            if( rSh.ActionPend() )
                return IdleJob::Busy;

        auto aCurrentClock = std::chrono::steady_clock::now();
        bool bNeedsBackup = !m_aLastBackup.has_value() ||
            (aCurrentClock - m_aLastBackup.value()) > std::chrono::seconds(60);
        if (bNeedsBackup && !m_sBackupPath.isEmpty()) {
            return IdleJob::AutoBackup;
        }

        if( pTmpRoot->IsNeedGrammarCheck() )
        {
            bool bIsOnlineSpell = pShell->GetViewOptions()->IsOnlineSpell();
            bool bIsAutoGrammar = false;
            SvtLinguConfig().GetProperty( UPN_IS_GRAMMAR_AUTO ) >>= bIsAutoGrammar;

            if( bIsOnlineSpell && bIsAutoGrammar && m_rDoc.StartGrammarChecking( true ) )
                return IdleJob::Grammar;
        }

        // If we're dragging re-layout doesn't occur so avoid a busy loop.
        if (!pShell->HasDrawViewDrag())
        {
            for ( auto pLayout : m_rDoc.GetAllLayouts() )
            {
                if( pLayout->IsIdleFormat() )
                    return IdleJob::Layout;
            }
        }

        if(m_rDoc.getIDocumentFieldsAccess().GetUpdateFields().IsFieldsDirty() )
        {
            if( m_rDoc.getIDocumentFieldsAccess().GetUpdateFields().IsInUpdateFields()
                    || m_rDoc.getIDocumentFieldsAccess().IsExpFieldsLocked() )
            {
                return IdleJob::Busy;
            }
            return IdleJob::Fields;
        }
    }

    return IdleJob::None;
}

void DocumentTimerManager::SetBackupPath(const OUString& path)
{
    m_sBackupPath = path;
}

IMPL_LINK_NOARG( DocumentTimerManager, DoIdleJobs, Timer*, void )
{
#ifdef TIMELOG
    static ::rtl::Logfile* pModLogFile = new ::rtl::Logfile( "First DoIdleJobs" );
#endif
    BlockIdling();
    StopIdling();

    IdleJob eJob = GetNextIdleJob();

    switch ( eJob )
    {
    case IdleJob::Grammar:
        m_rDoc.StartGrammarChecking();
        break;

    case IdleJob::Layout:
        for ( auto pLayout : m_rDoc.GetAllLayouts() )
            if( pLayout->IsIdleFormat() )
            {
                pLayout->GetCurrShell()->LayoutIdle();
                break;
            }
         break;

    case IdleJob::Fields:
    {
        SwViewShell* pShell( m_rDoc.getIDocumentLayoutAccess().GetCurrentViewShell() );
        SwRootFrame* pTmpRoot = m_rDoc.getIDocumentLayoutAccess().GetCurrentLayout();

        //  Action brackets!
        m_rDoc.getIDocumentFieldsAccess().GetUpdateFields().SetInUpdateFields( true );

        pTmpRoot->StartAllAction();

        // no jump on update of fields #i85168#
        const bool bOldLockView = pShell->IsViewLocked();
        pShell->LockView( true );

        auto pChapterFieldType = m_rDoc.getIDocumentFieldsAccess().GetSysFieldType( SwFieldIds::Chapter );
        pChapterFieldType->CallSwClientNotify(sw::LegacyModifyHint( nullptr, nullptr ));  // ChapterField
        m_rDoc.getIDocumentFieldsAccess().UpdateExpFields( nullptr, false );  // Updates ExpressionFields
        m_rDoc.getIDocumentFieldsAccess().UpdateTableFields(nullptr);  // Tables
        m_rDoc.getIDocumentFieldsAccess().UpdateRefFields();  // References

        // Validate and update the paragraph signatures.
        if (SwEditShell* pSh = m_rDoc.GetEditShell())
            pSh->ValidateAllParagraphSignatures(true);

        pTmpRoot->EndAllAction();

        pShell->LockView( bOldLockView );

        m_rDoc.getIDocumentFieldsAccess().GetUpdateFields().SetInUpdateFields( false );
        m_rDoc.getIDocumentFieldsAccess().GetUpdateFields().SetFieldsDirty( false );
        break;
    }
    case IdleJob::AutoBackup:
    {
        if (m_sBackupPath.isEmpty()) break;

        SwDocShell* pDocShell = m_rDoc.GetDocShell();
        auto aCurrentTime = std::chrono::steady_clock::now();
        OUString sCurrentTimestamp = OUString::number(aCurrentTime.time_since_epoch().count());

        if (!pDocShell)
        {
            break;
        }

        try
        {
            uno::Reference<frame::XStorable> xStorable(pDocShell->GetModel(), uno::UNO_QUERY_THROW);
            utl::MediaDescriptor aSaveMediaDescriptor;
            OUString aFilterName = "MS Word 2007 XML";
            aSaveMediaDescriptor["Overwrite"] <<= true;
            aSaveMediaDescriptor["FilterName"] <<= aFilterName;
            OUString sBackupURL = "file:///" + m_sBackupPath + "backup" + sCurrentTimestamp + ".docx";
            xStorable->storeToURL(sBackupURL, aSaveMediaDescriptor.getAsConstPropertyValueList());
            m_aLastBackup = aCurrentTime;
        }
        catch (const uno::Exception& /* exception */)
        {
            css::uno::Any exAny( cppu::getCaughtException() );
            SAL_WARN("macro.autobackup", "Failed to make backup: " << exceptionToString(exAny));
        }
    }
    break;
    case IdleJob::Busy:
        break;
    case IdleJob::None:
        break;
    }

    if ( IdleJob::None != eJob )
        StartIdling();
    UnblockIdling();

#ifdef TIMELOG
    if( pModLogFile && 1 != (long)pModLogFile )
        delete pModLogFile, static_cast<long&>(pModLogFile) = 1;
#endif
}

DocumentTimerManager::~DocumentTimerManager() {}

void DocumentTimerManager::MarkLOKIdle()
{
    bool wasWaiting = m_bWaitForLokInit;
    if (wasWaiting && !m_aDocIdle.IsActive()) {
        m_aDocIdle.SetTimeout(kDebounceRateMilliseconds);
    }
    m_bWaitForLokInit = false;
}

}


/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
