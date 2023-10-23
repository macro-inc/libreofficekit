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

#include <atomic>
#include <svl/colorizer.hxx>

namespace colorizer
{

namespace
{
std::atomic<bool>& getPoolingBlocked()
{
    static std::atomic<bool> poolingBlocked = false;
    return poolingBlocked;
}
}

void SetBlockPooling(bool value) {
    getPoolingBlocked() = value;
}

// Checked when performing some style pool optimizations
bool IsPoolingBlocked() {
    return getPoolingBlocked();
}

}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
