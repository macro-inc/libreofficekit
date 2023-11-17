#pragma once
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 */

#include "comphelper/comphelperdllapi.h"
#include "osl/thread.h"
#include "rtl/ustring.hxx"
#include "sal/config.h"

#include <cassert>
#include <cstddef>
#include <functional>
#ifdef LIBO_INTERNAL_ONLY
#include <type_traits>
#include "com/sun/star/uno/Reference.h"
#endif

#include "sal/types.h"

namespace comphelper::originthread
{

using key_t = sal_uInt32;

class COMPHELPER_DLLPUBLIC Key
{
public:
    explicit Key(const char* debug = "");
    explicit operator OUString() const
    {
        return OUString::createFromAscii(debug_) + "(" + OUString::number(key_) + ")";
    }
    explicit operator key_t() const { return key_; }
    explicit operator const char*() const { return debug_; }
    explicit operator bool() const { return key_ != 0; }
    bool operator==(const Key& other) const { return key_ == other.key_; }
    bool operator!=(const Key& other) const { return key_ != other.key_; };
    Key(const Key& other) = default;
    ~Key();

private:
    key_t key_;
    const char* debug_;
};

COMPHELPER_DLLPUBLIC void setOriginValue(Key key, void* value);
COMPHELPER_DLLPUBLIC void* getOriginValue(Key key);

// gets or sets the origin hint for the thread, used for overriding the origin thread
COMPHELPER_DLLPUBLIC oslThreadIdentifier originHint(oslThreadIdentifier id = 0);

// expected to be used as static thread_local at namespace scope
template <typename T> class OriginPtr
{
public:
    OriginPtr() = delete;

    explicit OriginPtr(Key key, T* ptr = nullptr)
        : key_(key)
        , threadId_(osl_getThreadIdentifier(nullptr))
        , originThreadId_(originHint())
    {
        if (threadId_ == originThreadId_)
            setOriginValue(key_, ptr);
    }

    ~OriginPtr() {}

    // copy constructor
    OriginPtr(const OriginPtr& other)
        : key_(other.key_)
        , threadId_(osl_getThreadIdentifier(nullptr))
        , originThreadId_(originHint())
    {
    }

    // copy assignment
    OriginPtr& operator=(const OriginPtr& other)
    {
        if (this != &other)
        {
            key_ = other.key_;
            threadId_ = osl_getThreadIdentifier(nullptr);
            originThreadId_ = originHint();
        }
        return *this;
    }

    // disable move, the semantics don't really fit this usecase
    OriginPtr(OriginPtr&& other) noexcept = delete;
    OriginPtr& operator=(OriginPtr&& other) noexcept = delete;

    void clear()
    {
        if (osl_getThreadIdentifier(nullptr) == originThreadId_)
            setOriginValue(key_, nullptr);
    }

    void reset(T* ptr = nullptr)
    {
        if (osl_getThreadIdentifier(nullptr) != originThreadId_)
            return;

        T* oldPtr = static_cast<T*>(getOriginValue(key_));
        setOriginValue(key_, ptr);
        if (oldPtr)
            delete oldPtr;
    }

    T* get() const { return static_cast<T*>(getOriginValue(key_)); }
    T& operator*() const { return *static_cast<T*>(getOriginValue(key_)); }
    T* operator->() const { return static_cast<T*>(getOriginValue(key_)); }
    bool is() const { return key_ && getOriginValue(key_) != nullptr; }
    explicit operator bool() const { return is(); }
    bool operator==(const OriginPtr& other) const { return key_ == other.key_; }
    bool operator!=(const OriginPtr& other) const { return key_ != other.key_; };

protected:
    Key key_;
    oslThreadIdentifier threadId_;
    oslThreadIdentifier originThreadId_;
};

// return value expected to be used as static thread_local at namespace scope
template <typename T, typename... Args> OriginPtr<T> make_at_origin(Key key = Key(), Args&&... args)
{
    OriginPtr<T> result(key);
    if (osl_getThreadIdentifier(nullptr) == originHint())
    {
        result.reset(new T(std::forward<Args>(args)...));
    }
    return result;
}

/** Template reference class for reference type, structure mostly from rtl::Reference
Assumes sets are thread-safe already
*/
template <class T> class OriginReference : public OriginPtr<T>
{
public:
    OriginReference() = delete;

    explicit OriginReference(Key key, T* ptr = nullptr)
        : OriginPtr<T>(key, ptr)
    {
    }
    // copy constructor
    OriginReference(const OriginReference<T>& handle)
        : OriginPtr<T>(handle)
    {
        if (this->is())
            this->get()->acquire();
    }
    // disable move constructor
    OriginReference(OriginReference<T>&& handle) noexcept = delete;
    // disable move assignment
    OriginReference<T>& operator=(OriginReference<T>&& handle) = delete;

    /** Up-casting conversion constructor: Copies interface reference.
        Does not work for up-casts to ambiguous bases.
        @param rRef another reference
    */
    template <class derived_type>
    inline OriginReference(const OriginReference<derived_type>& rRef,
                           std::enable_if_t<std::is_base_of_v<T, derived_type>, int> = 0)
        : OriginPtr<derived_type>(rRef.key_, rRef.get())
    {
        if (this->is())
            this->get()->acquire();
    }

    /** Up-casting conversion operator to convert to css::uno::Interface

        Does not work for up-casts to ambiguous bases.
    */
    template <class super_type, std::enable_if_t<std::is_base_of_v<super_type, T>, int> = 0>
    inline operator css::uno::Reference<super_type>() const
    {
        return css::uno::Reference<super_type>(this->get());
    }

    /** Destructor... */
    ~OriginReference() COVERITY_NOEXCEPT_FALSE
    {
        if (this->is())
            this->get()->release();
    }

    OriginReference<T>& set(T* pBody)
    {
        if (pBody)
            pBody->acquire();
        T* const pOld = this->get();
        setOriginValue(this->key_, pBody);
        if (pOld)
            pOld->release();
        return *this;
    }

    /** Assignment.
         Unbinds this instance from its body (if bound) and
         bind it to the body represented by the handle.
     */
    OriginReference<T>& operator=(const OriginReference<T>& handle) { return set(handle.get()); }

    /** Assignment... */
    OriginReference<T>& operator=(T* pBody) { return set(pBody); }

    OriginReference<T>& operator=(const css::uno::Reference<T>& handle)
    {
        return set(handle.get());
    }

    /** Unbind the body from this handle. */
    OriginReference<T>& clear()
    {
        if (this->is())
        {
            T* const pOld = this->get();
            setOriginValue(this->key_, nullptr);
            pOld->release();
        }
        return *this;
    }
};

}
