/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of Macro's fork of LibreOffice
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 */
// MACRO-1598: Colorizer slow
#pragma once
#include <svl/svldllapi.h>

namespace colorizer {

SVL_DLLPUBLIC void SetBlockPooling(bool value);

// Checked when performing some style pool optimizations
bool IsPoolingBlocked();

}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
