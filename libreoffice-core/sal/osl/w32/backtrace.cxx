/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "sal/types.h"
#include <sal/config.h>

#include <limits>
#include <memory>

#if !defined WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <process.h>
#include <iostream>
#define OPTIONAL
#include <DbgHelp.h>

#include <rtl/ustrbuf.hxx>
#include <sal/backtrace.hxx>

#include <backtraceasstring.hxx>

namespace
{

template <typename T> T clampToULONG(T n)
{
    auto const maxUlong = std::numeric_limits<ULONG>::max();
    return n > maxUlong ? static_cast<T>(maxUlong) : n;
}

std::string_view basename(const std::string_view& file)
{
    size_t i = file.find_last_of("\\/");
    if (i == std::string_view::npos)
    {
        return file;
    }
    else
    {
        return file.substr(i + 1);
    }
}

std::wstring_view basename(const std::wstring_view& file)
{
    size_t i = file.find_last_of(L"\\/");
    if (i == std::wstring_view::npos)
    {
        return file;
    }
    else
    {
        return file.substr(i + 1);
    }
}

constexpr DWORD kStatusInfoLengthMismatch = 0xC0000004;
constexpr size_t kSymInitializeRetryCount = 3;
const size_t PATH_MAX = 1024;

// sets relative paths for PDB adjacent to process EXE/DLLs and LOK DLLs, its okay if any part of this fails
void SymSetPath(HANDLE handle) {
    wchar_t path[PATH_MAX];
    if (!::SymGetSearchPathW(handle, path, PATH_MAX)) return;
    std::wstring new_path(path);
    
    if (GetModuleFileNameW( NULL, path, PATH_MAX )) {
        std::wstring_view base = basename(path);
        new_path += L";";
        new_path += base;
        new_path += L";";
        new_path += base;
        new_path += L"\\libreofficekit";
    }

    ::SymSetSearchPathW(handle, new_path.c_str());
}

bool SymInitializeWrapper(HANDLE handle, BOOL invade_process)
{
    SymSetOptions(SYMOPT_DEFERRED_LOADS |
            SYMOPT_UNDNAME |
            SYMOPT_LOAD_LINES);
    for (size_t i = 0; i < kSymInitializeRetryCount; ++i)
    {
        if (SymInitialize(handle, nullptr, invade_process)) {
            SymSetPath(handle);
            return true;
        }

        DWORD init_error = GetLastError();
        if (init_error != kStatusInfoLengthMismatch)
            return false;
    }

    return false;
}

}

OUString osl::detail::backtraceAsString(sal_uInt32 maxDepth)
{
    std::unique_ptr<sal::BacktraceState> backtrace = sal::backtrace_get(maxDepth);
    return sal::backtrace_to_string(backtrace.get());
}

std::unique_ptr<sal::BacktraceState> sal::backtrace_get(sal_uInt32 maxDepth)
{
    assert(maxDepth != 0);
    maxDepth = clampToULONG(maxDepth);

    auto pStack = new void*[maxDepth];
    // https://msdn.microsoft.com/en-us/library/windows/desktop/bb204633.aspx
    // "CaptureStackBackTrace function" claims that you "can capture up to
    // MAXUSHORT frames", and on Windows Server 2003 and Windows XP it even
    // "must be less than 63", but assume that a too large input value is
    // clamped internally, instead of resulting in an error:
    int nFrames = CaptureStackBackTrace(0, static_cast<ULONG>(maxDepth), pStack, nullptr);

    return std::unique_ptr<BacktraceState>(new BacktraceState{ pStack, nFrames });
}

OUString sal::backtrace_to_string(BacktraceState* backtraceState)
{
    HANDLE hProcess = GetCurrentProcess();
    // https://docs.microsoft.com/en-us/windows/win32/api/dbghelp/nf-dbghelp-syminitialize
    // says to not initialize more than once.
    static bool bInitialized = SymInitializeWrapper(hProcess, false);
    if (!bInitialized)
        return "Unable to initialize symbols and collect stack trace";
    SymRefreshModuleList(hProcess);
    SYMBOL_INFO* pSymbol;
    pSymbol = static_cast<SYMBOL_INFO*>(calloc(sizeof(SYMBOL_INFO) + 1024 * sizeof(char), 1));
    if (!pSymbol)
        return "Unable to allocate for symbol information and collect stack trace";
    pSymbol->MaxNameLen = 1024 - 1;
    pSymbol->SizeOfStruct = sizeof(SYMBOL_INFO);

    auto nFrames = backtraceState->nDepth;
    char module[MAX_PATH];
    OUStringBuffer aBuf;
    DWORD line_displacement = 0;
    IMAGEHLP_LINE64 line = {};
    line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);

    constexpr int offset = 11;
    for (int i = offset; i < nFrames; i++)
    {
        HMODULE addr = reinterpret_cast<HMODULE>(backtraceState->buffer[i]);
        HMODULE modAddr = 0;
        // We have a symbol with filename and location
        if (::SymFromAddr(hProcess, (DWORD64)addr, nullptr,
                        pSymbol) && SymGetLineFromAddr64(hProcess, (DWORD64)addr, &line_displacement, &line))
        {
            aBuf.append('#');
            aBuf.append(static_cast<sal_Int32>(i - offset));
            aBuf.append(' ');
            aBuf.appendAscii(pSymbol->Name);
            aBuf.append(" at ");
            aBuf.appendAscii(line.FileName);
            aBuf.append(":");
            aBuf.append(static_cast<sal_Int32>(line.LineNumber));
            aBuf.append("\n");
        }
        else if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCSTR>(addr), &modAddr)) {
            aBuf.append('#');
            aBuf.append(static_cast<sal_Int32>(i - offset));
            aBuf.append(" [+0x");
            aBuf.append(OUString::number(reinterpret_cast<sal_uInt64>(addr) - reinterpret_cast<sal_uInt64>(modAddr), 16));
            aBuf.append("](");
            if (GetModuleFileNameA(modAddr, module, MAX_PATH))
            {
                aBuf.append(OUString::fromUtf8(basename(module)));
            } else {
                aBuf.append("???\n");
            }
            aBuf.append(")\n");
        } else {
            aBuf.append('#');
            aBuf.append(static_cast<sal_Int32>(i - offset));
            aBuf.append(" ???\n");
        }
    }

    free(pSymbol);

    return aBuf.makeStringAndClear();
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
