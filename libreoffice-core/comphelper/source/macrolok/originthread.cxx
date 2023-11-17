/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 */

#include "sal/types.h"
#include "sal/log.hxx"
#include <atomic>
#include <comphelper/originthread.hxx>

namespace comphelper::originthread
{

std::atomic<key_t> g_key(1);

Key::Key(const char* debug)
    : key_(g_key++)
    , debug_(debug)
{
    // SAL_WARN("start", "key " << debug_ << " = " << key_);
}

Key::~Key() = default;

inline sal_uInt64 deriveKey(sal_uInt32 a, sal_uInt32 b) { return (sal_uInt64(a) << 32) | b; }

std::unordered_map<sal_uInt64, void*> g_map{};

oslThreadIdentifier originHint(oslThreadIdentifier id)
{
    static thread_local oslThreadIdentifier id_ = osl_getOriginIdentifier();
    if (id)
    {
        // SAL_WARN("start", "origin hint setting id " << id);
        id_ = id;
    }
    // SAL_WARN("start", "origin hint " << id_);
    return id_;
}

// sets _should be_ infrequent, these used to be globals after all
void setOriginValue(Key key, void* value)
{
    auto oh = originHint();
    auto dk = deriveKey((key_t)key, oh);
    if (value == nullptr) {
        SAL_WARN("start", "setting key ["  << (OUString)key <<  "] to null on [" << oh << "]\n");
    }
    // SAL_WARN("start", "set origin value: key " << (OUString)key << " oh " << oh << " dk " << dk << " val = " << (size_t)value);
    g_map[dk] = value;
}

// assume that there will be repeated local fetches of the same key, such as a global mutex
void* getOriginValue(Key key)
{
    auto oh = originHint();
    auto k = (key_t)key;
    auto dk = deriveKey(k, oh);
    auto* ret = g_map[dk];
    // SAL_WARN("start", "get origin value: key " << (OUString)key << " oh " << oh << " dk " << dk << " ret = " << (size_t)ret);
    return ret;
}

}
