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

#include "IDocumentOutlineNodes.hxx"
#include "itabenum.hxx"
#include "ndtxt.hxx"
#include "txtfrm.hxx"
#include "wrtsh.hxx"
#include <iostream>
#include <unotxdoc.hxx>

#include <map>
#include <utility>
#include <vector>

#include <com/sun/star/beans/XPropertyAccess.hpp>

#include <comphelper/sequence.hxx>
#include <o3tl/string_view.hxx>
#include <tools/json_writer.hxx>
#include <tools/urlobj.hxx>
#include <xmloff/odffields.hxx>

#include <IDocumentMarkAccess.hxx>
#include <doc.hxx>
#include <docsh.hxx>

using namespace ::com::sun::star;

namespace
{
/// Implements getCommandValues(".uno:TextFormFields").
///
/// Parameters:
///
/// - type: e.g. ODF_UNHANDLED
/// - commandPrefix: field comment prefix not not return all fieldmarks
void GetTextFormFields(tools::JsonWriter& rJsonWriter, SwDocShell* pDocShell,
                       const std::map<OUString, OUString>& rArguments)
{
    OUString aType;
    OUString aCommandPrefix;
    {
        auto it = rArguments.find("type");
        if (it != rArguments.end())
        {
            aType = it->second;
        }

        it = rArguments.find("commandPrefix");
        if (it != rArguments.end())
        {
            aCommandPrefix = it->second;
        }
    }

    SwDoc* pDoc = pDocShell->GetDoc();
    IDocumentMarkAccess* pMarkAccess = pDoc->getIDocumentMarkAccess();
    tools::ScopedJsonWriterArray aFields = rJsonWriter.startArray("fields");
    for (auto it = pMarkAccess->getFieldmarksBegin(); it != pMarkAccess->getFieldmarksEnd(); ++it)
    {
        auto pFieldmark = dynamic_cast<sw::mark::IFieldmark*>(*it);
        assert(pFieldmark);
        if (pFieldmark->GetFieldname() != aType)
        {
            continue;
        }

        auto itParam = pFieldmark->GetParameters()->find(ODF_CODE_PARAM);
        if (itParam == pFieldmark->GetParameters()->end())
        {
            continue;
        }

        OUString aCommand;
        itParam->second >>= aCommand;
        if (!aCommand.startsWith(aCommandPrefix))
        {
            continue;
        }

        tools::ScopedJsonWriterStruct aField = rJsonWriter.startStruct();
        rJsonWriter.put("type", aType);
        rJsonWriter.put("command", aCommand);
    }
}

/// Implements getCommandValues(".uno:SetDocumentProperties").
///
/// Parameters:
///
/// - namePrefix: field name prefix to not return all user-defined properties
void GetDocumentProperties(tools::JsonWriter& rJsonWriter, SwDocShell* pDocShell,
                           const std::map<OUString, OUString>& rArguments)
{
    OUString aNamePrefix;
    auto it = rArguments.find("namePrefix");
    if (it != rArguments.end())
    {
        aNamePrefix = it->second;
    }

    uno::Reference<document::XDocumentPropertiesSupplier> xDPS(pDocShell->GetModel(),
                                                               uno::UNO_QUERY);
    uno::Reference<document::XDocumentProperties> xDP = xDPS->getDocumentProperties();
    uno::Reference<beans::XPropertyAccess> xUDP(xDP->getUserDefinedProperties(), uno::UNO_QUERY);
    auto aUDPs = comphelper::sequenceToContainer<std::vector<beans::PropertyValue>>(
        xUDP->getPropertyValues());
    tools::ScopedJsonWriterArray aProperties = rJsonWriter.startArray("userDefinedProperties");
    for (const auto& rUDP : aUDPs)
    {
        if (!rUDP.Name.startsWith(aNamePrefix))
        {
            continue;
        }

        if (rUDP.Value.getValueTypeClass() != uno::TypeClass_STRING)
        {
            continue;
        }

        OUString aValue;
        rUDP.Value >>= aValue;

        tools::ScopedJsonWriterStruct aProperty = rJsonWriter.startStruct();
        rJsonWriter.put("name", rUDP.Name);
        rJsonWriter.put("type", "string");
        rJsonWriter.put("value", aValue);
    }
}

/// Implements getCommandValues(".uno:Bookmarks").
///
/// Parameters:
///
/// - namePrefix: bookmark name prefix to not return all bookmarks
void GetBookmarks(tools::JsonWriter& rJsonWriter, SwDocShell* pDocShell,
                  const std::map<OUString, OUString>& rArguments)
{
    OUString aNamePrefix;
    {
        auto it = rArguments.find("namePrefix");
        if (it != rArguments.end())
        {
            aNamePrefix = it->second;
        }
    }

    IDocumentMarkAccess& rIDMA = *pDocShell->GetDoc()->getIDocumentMarkAccess();
    tools::ScopedJsonWriterArray aBookmarks = rJsonWriter.startArray("bookmarks");
    for (auto it = rIDMA.getBookmarksBegin(); it != rIDMA.getBookmarksEnd(); ++it)
    {
        sw::mark::IMark* pMark = *it;
        if (!pMark->GetName().startsWith(aNamePrefix))
        {
            continue;
        }

        tools::ScopedJsonWriterStruct aProperty = rJsonWriter.startStruct();
        rJsonWriter.put("name", pMark->GetName());
    }
}

/// Implements getCommandValues(".uno:GetOutline").
void GetOutline(tools::JsonWriter& rJsonWriter, SwDocShell* pDocShell)
{
    SwWrtShell* mrSh = pDocShell->GetWrtShell();

    const SwOutlineNodes::size_type nOutlineCount
        = mrSh->getIDocumentOutlineNodesAccess()->getOutlineNodesCount();

    typedef std::pair<sal_Int8, sal_Int32> StackEntry;
    std::stack<StackEntry> aOutlineStack;
    aOutlineStack.push(StackEntry(-1, -1)); // push default value

    tools::ScopedJsonWriterArray aOutline = rJsonWriter.startArray("outline");

    // Allows for 65535 nodes in the outline
    sal_uInt16 nOutlineId = 0;

    for (SwOutlineNodes::size_type i = 0; i < nOutlineCount; ++i)
    {
        // Check if outline is hidden
        const SwTextNode* textNode = mrSh->GetNodes().GetOutLineNds()[i]->GetTextNode();

        if (textNode->IsHidden() || !sw::IsParaPropsNode(*mrSh->GetLayout(), *textNode) ||
            // Skip empty outlines:
            textNode->GetText().isEmpty())
        {
            nOutlineId++;
            continue;
        }

        // Get parent id from stack:
        const sal_Int8 nLevel
            = static_cast<sal_Int8>(mrSh->getIDocumentOutlineNodesAccess()->getOutlineLevel(i));

        sal_Int8 nLevelOnTopOfStack = aOutlineStack.top().first;
        while (nLevelOnTopOfStack >= nLevel && nLevelOnTopOfStack != -1)
        {
            aOutlineStack.pop();
            nLevelOnTopOfStack = aOutlineStack.top().first;
        }

        const sal_Int32 nParent = aOutlineStack.top().second;

        tools::ScopedJsonWriterStruct aProperty = rJsonWriter.startStruct();

        // We need to explicitly get the text here in case there are special characters
        // Using textNode->GetText() will result in a failure to convert the result to JSON in the DocumentClient::GetCommandValues in electron-libreoffice
        const OUString& rEntry = mrSh->getIDocumentOutlineNodesAccess()->getOutlineText(i, mrSh->GetLayout(), true, false, false );

        rJsonWriter.put("id", nOutlineId);
        rJsonWriter.put("parent", nParent);
        rJsonWriter.put("text", rEntry);

        aOutlineStack.push(StackEntry(nLevel, nOutlineId));

        nOutlineId++;
    }
}
}

void SwXTextDocument::getCommandValues(tools::JsonWriter& rJsonWriter, const OString& rCommand)
{
    std::map<OUString, OUString> aMap;

    static constexpr OStringLiteral aTextFormFields(".uno:TextFormFields");
    static constexpr OStringLiteral aSetDocumentProperties(".uno:SetDocumentProperties");
    static constexpr OStringLiteral aBookmarks(".uno:Bookmarks");
    static constexpr OStringLiteral aGetOutline(".uno:GetOutline");

    INetURLObject aParser(OUString::fromUtf8(rCommand));
    OUString aArguments = aParser.GetParam();
    sal_Int32 nParamIndex = 0;
    do
    {
        OUString aParam = aArguments.getToken(0, '&', nParamIndex);
        sal_Int32 nIndex = 0;
        OUString aKey;
        OUString aValue;
        do
        {
            OUString aToken = aParam.getToken(0, '=', nIndex);
            if (aKey.isEmpty())
                aKey = aToken;
            else
                aValue = aToken;
        } while (nIndex >= 0);
        OUString aDecodedValue
            = INetURLObject::decode(aValue, INetURLObject::DecodeMechanism::WithCharset);
        aMap[aKey] = aDecodedValue;
    } while (nParamIndex >= 0);

    if (o3tl::starts_with(rCommand, aTextFormFields))
    {
        GetTextFormFields(rJsonWriter, m_pDocShell, aMap);
    }
    else if (o3tl::starts_with(rCommand, aSetDocumentProperties))
    {
        GetDocumentProperties(rJsonWriter, m_pDocShell, aMap);
    }
    else if (o3tl::starts_with(rCommand, aBookmarks))
    {
        GetBookmarks(rJsonWriter, m_pDocShell, aMap);
    }
    else if (o3tl::starts_with(rCommand, aGetOutline))
    {
        GetOutline(rJsonWriter, m_pDocShell);
    }
}

void SwXTextDocument::gotoOutline(tools::JsonWriter& rJsonWriter, int idx)
{
    SwWrtShell* mrSh = m_pDocShell->GetWrtShell();

    mrSh->GotoOutline(idx);

    SwRect destRect = mrSh->GetCharRect();

    rJsonWriter.put("destRect", destRect.SVRect().toString());
}

void SwXTextDocument::setReadOnly()
{
    m_pDocShell->GetObjectShell()->SetReadOnly();
}

void SwXTextDocument::createTable(int row, int col)
{
    SwWrtShell* mrSh = m_pDocShell->GetWrtShell();

    const SwInsertTableOptions aInsertTableOptions(SwInsertTableFlags::DefaultBorder,/*nRowsToRepeat=*/0);

    mrSh->InsertTable(aInsertTableOptions, row, col);
}

void SwXTextDocument::initializeAppearanceFlags()
{
    SwViewOption::SetAppearanceFlag(ViewOptFlags::DocBoundaries, false, false);
    SwViewOption::SetAppearanceFlag(ViewOptFlags::TableBoundaries, false, false);
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
