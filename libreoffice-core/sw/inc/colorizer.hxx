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
#ifndef INCLUDED_SW_INC_COLORIZER_HXX
#define INCLUDED_SW_INC_COLORIZER_HXX

#include <optional>
#include <string>
#include <unordered_map>
#include <unotxdoc.hxx>
#include <doc.hxx>

namespace colorizer
{

enum OverlayType
{
    START = 0,
    TERM_OVERLAY = 1,
    TERMREF_OVERLAY = 2,
    ANOMALY_OVERLAY = 3,
    END = 4
};

void Colorize(rtl::Reference<SwXTextDocument> doc);
void CancelColorize(rtl::Reference<SwXTextDocument> doc);

void ApplyOverlays(rtl::Reference<SwXTextDocument> doc, const char* json);
void ClearOverlays(rtl::Reference<SwXTextDocument> doc, const char* json);
void JumpToOverlay(rtl::Reference<SwXTextDocument> doc, const char* json);

}

#endif //INCLUDED_SW_INC_COLORIZER_HXX
/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
