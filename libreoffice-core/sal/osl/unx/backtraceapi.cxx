/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <sal/config.h>

#include <cassert>
#include <cstdlib>
#include <limits>
#include <memory>
#include <mutex>

#include <o3tl/runtimetooustring.hxx>
#include <rtl/ustrbuf.hxx>
#include <rtl/ustring.hxx>
#include <sal/types.h>
#include <sal/log.hxx>
#include <sal/backtrace.hxx>

#include "backtrace.h"
#include <backtraceasstring.hxx>
#include <string_view>

OUString osl::detail::backtraceAsString(sal_uInt32 maxDepth)
{
    std::unique_ptr<sal::BacktraceState> backtrace = sal::backtrace_get(maxDepth);
    return sal::backtrace_to_string(backtrace.get());
}

std::unique_ptr<sal::BacktraceState> sal::backtrace_get(sal_uInt32 maxDepth)
{
    assert(maxDepth != 0);
    auto const maxInt = static_cast<unsigned int>(std::numeric_limits<int>::max());
    if (maxDepth > maxInt)
    {
        maxDepth = static_cast<sal_uInt32>(maxInt);
    }
    auto b1 = new void*[maxDepth];
    int n = backtrace(b1, static_cast<int>(maxDepth));
    return std::unique_ptr<BacktraceState>(new BacktraceState{ b1, n });
}

#if (defined LINUX || defined MACOSX || defined FREEBSD || defined NETBSD || defined OPENBSD       \
     || defined(DRAGONFLY))
// The backtrace_symbols() function is unreliable, it requires -rdynamic and even then it cannot resolve names
// of many functions, such as those with hidden ELF visibility. Libunwind doesn't resolve names for me either,
// boost::stacktrace doesn't work properly, the best result I've found is addr2line. Using addr2line is relatively
// slow, but I don't find that to be a big problem for printing of backtraces. Feel free to improve if needed
// (e.g. the calls could be grouped by the binary).
#include <dlfcn.h>
#include <unistd.h>
#include <vector>
#include <osl/process.h>
#include <rtl/strbuf.hxx>
#include <osl/mutex.hxx>
#include <o3tl/lru_map.hxx>
#include "file_url.hxx"

namespace
{
struct FrameData
{
    std::string_view file;
    void* addr;
    ptrdiff_t offset;
    OString info;
    bool handled = false;
};

typedef o3tl::lru_map<void*, OString> FrameCache;
std::mutex frameCacheMutex;
FrameCache frameCache(64);

std::string_view basename(std::string_view path)
{
    auto split_pos = path.rfind('/');
    if (split_pos != std::string_view::npos)
    {
        return path.substr(split_pos + 1);
    }
    return {};
}

void process_file_addr2line(std::string_view file, std::vector<FrameData>& frameData)
{
    std::vector<OUString> addrs;
    std::vector<rtl_uString*> args;
    OUString dummy;
#ifdef MACOSX
    OUString binary("atos");
    if (!osl::detail::find_in_PATH(binary, dummy))
    {
        for (FrameData& frame : frameData)
        {
            if (!frame.file.empty() && file == frame.file)
            {
                frame.info
                    = "[+0x" + OString::number(frame.offset, 16) + "]" + OString(basename(file));
                std::lock_guard guard(frameCacheMutex);
                frameCache.insert({ frame.addr, frame.info });
            }
        }
        return;
    }
    OUString arg1("--fullPath");
    OUString arg2("--offset");
    OUString arg3("-o");
    OUString arg4 = OUString::fromUtf8(file);
    args.reserve(frameData.size() + 4);
    args.push_back(arg1.pData);
    args.push_back(arg2.pData);
    args.push_back(arg3.pData);
    args.push_back(arg4.pData);
#else
    OUString binary("llvm-symbolizer");
    if (osl::detail::find_in_PATH("llvm-symbolizer-14", dummy))
        binary = "llvm-symbolizer-14";
    if (osl::detail::find_in_PATH("llvm-symbolizer-15", dummy))
        binary = "llvm-symbolizer-15";
    if (osl::detail::find_in_PATH("llvm-symbolizer-16", dummy))
        binary = "llvm-symbolizer-16";
    if (!osl::detail::find_in_PATH(binary, dummy))
    {
        for (FrameData& frame : frameData)
        {
            if (!frame.file.empty() && file == frame.file)
            {
                frame.info
                    = "[+0x" + OString::number(frame.offset, 16) + "](" + OString(basename(file)) + ")";
                std::lock_guard guard(frameCacheMutex);
                frameCache.insert({ frame.addr, frame.info });
            }
        }
        return;
    }
    OUString arg1("-Cfpe");
    OUString arg2 = OUString::fromUtf8(file);
    args.reserve(frameData.size() + 2);
    args.push_back(arg1.pData);
    args.push_back(arg2.pData);
#endif
    for (FrameData& frame : frameData)
    {
        if (!frame.file.empty() && file == frame.file)
        {
            addrs.push_back("0x" + OUString::number(frame.offset, 16));
            args.push_back(addrs.back().pData);
            frame.handled = true;
        }
    }

    oslProcess aProcess;
    oslFileHandle pOut = nullptr;
    oslFileHandle pErr = nullptr;
    oslSecurity pSecurity = osl_getCurrentSecurity();
    oslProcessError eErr = osl_executeProcess_WithRedirectedIO(
        binary.pData, args.data(), args.size(), osl_Process_SEARCHPATH | osl_Process_HIDDEN,
        pSecurity, nullptr, nullptr, 0, &aProcess, nullptr, &pOut, &pErr);
    osl_freeSecurityHandle(pSecurity);

    if (eErr != osl_Process_E_None)
    {
        SAL_WARN("sal.osl", binary << " call to resolve " << file << " symbols failed");
        return;
    }

    OStringBuffer outputBuffer;
    if (pOut)
    {
        const sal_uInt64 BUF_SIZE = 1024;
        char buffer[BUF_SIZE];
        while (true)
        {
            sal_uInt64 bytesRead = 0;
            while (osl_readFile(pErr, buffer, BUF_SIZE, &bytesRead) == osl_File_E_None
                   && bytesRead != 0)
                ; // discard possible stderr output
            oslFileError err = osl_readFile(pOut, buffer, BUF_SIZE, &bytesRead);
            if (bytesRead == 0 && err == osl_File_E_None)
                break;
            outputBuffer.append(buffer, bytesRead);
            if (err != osl_File_E_None && err != osl_File_E_AGAIN)
                break;
        }
        osl_closeFile(pOut);
    }
    if (pErr)
        osl_closeFile(pErr);
    eErr = osl_joinProcess(aProcess);
    osl_freeProcessHandle(aProcess);

    OString output = outputBuffer.makeStringAndClear();
    std::vector<OString> lines;
    sal_Int32 outputPos = 0;
    while (outputPos < output.getLength())
    {
        sal_Int32 end1 = output.indexOf('\n', outputPos);
        if (end1 < 0)
            break;
        sal_Int32 end2 = output.indexOf('\n', end1 + 1);
        if (end2 < 0)
            end2 = output.getLength();
        lines.push_back(output.copy(outputPos, end1 - outputPos));
        lines.push_back(output.copy(end1 + 1, end2 - end1 - 1));
        outputPos = end2 + 1;
    }
    size_t linesPos = 0;
#ifndef MACOSX
    static const std::string_view BAD_MATCH = " at ??:";
#endif

    for (FrameData& frame : frameData)
    {
        if (!frame.file.empty() && file == frame.file)
        {
            if (linesPos >= lines.size())
                break;
#ifdef MACOSX
            // bad match is sometimes empty, sometimes repeats the offset
            OString offset = "0x" + OString::number(frame.offset, 16);
            if(lines[linesPos].getLength() <= 2 || lines[linesPos] == offset) {
                frame.info
                    = "[+0x" + OString::number(frame.offset, 16) + "](" + OString(basename(file)) + ")";
            } else if(lines[linesPos].indexOf("+") != -1) {
                // no line numbers, just an offset inside the function, not that useful, so give the offeset too
                frame.info
                    = "[+0x" + OString::number(frame.offset, 16) + "](" + OString(basename(file)) + ") " + lines[linesPos];
            } else {
                frame.info = lines[linesPos];
            }
            linesPos++;
#else
            OStringBuffer lineBuffer;
            if (lines[linesPos].indexOf(BAD_MATCH) != -1)
            {
                // skip to next
                while (linesPos < lines.size() && lines[linesPos].getLength() > 1)
                    linesPos++;
                ++linesPos;
                frame.info
                    = "[+0x" + OString::number(frame.offset, 16) + "](" + OString(basename(file)) + ")";
                std::lock_guard guard(frameCacheMutex);
                frameCache.insert({ frame.addr, frame.info });
                continue;
            }

            while (linesPos < lines.size() && lines[linesPos].getLength() <= 1)
                linesPos++;

            while (linesPos < lines.size() && lines[linesPos].getLength() > 1)
            {
                lineBuffer.append(lines[linesPos]);
                if (++linesPos < lines.size() && lines[linesPos].getLength() > 1)
                {
                    lineBuffer.append('\n');
                }
            }
            ++linesPos;
            frame.info = lineBuffer.makeStringAndClear();
#endif
            std::lock_guard guard(frameCacheMutex);
            frameCache.insert({ frame.addr, frame.info });
        }
    }
}

} // namespace

OUString sal::backtrace_to_string(BacktraceState* backtraceState)
{
    // Collect frames for each binary and process each binary in one addr2line
    // call for better performance.
    std::vector<FrameData> frameData;
    frameData.resize(backtraceState->nDepth);
    for (int i = 0; i != backtraceState->nDepth; ++i)
    {
        Dl_info dli;
        void* addr = backtraceState->buffer[i];
        std::unique_lock guard(frameCacheMutex);
        auto it = frameCache.find(addr);
        bool found = it != frameCache.end();
        guard.unlock();
        if (found)
        {
            frameData[i].info = it->second;
            frameData[i].handled = true;
        }
        else if (dladdr(addr, &dli) != 0)
        {
            if (dli.dli_fname && dli.dli_fbase)
            {
                std::string_view file(dli.dli_fname);
                // split at the argument boundary
                auto split_pos = file.find(" --");
                if (split_pos != std::string_view::npos)
                {
                    file = file.substr(0, split_pos);
                }
                frameData[i].file = file;
                frameData[i].addr = addr;
                frameData[i].offset = reinterpret_cast<ptrdiff_t>(addr)
                                      - reinterpret_cast<ptrdiff_t>(dli.dli_fbase);
            }
        }
    }
    for (int j = 0; j != backtraceState->nDepth; ++j)
    {
        int i = j - 0;
        if (!frameData[i].file.empty() && !frameData[i].handled)
        {
            process_file_addr2line(frameData[i].file, frameData);
        }
    }
    OUStringBuffer b3;
    std::unique_ptr<char*, decltype(free)*> b2{ nullptr, free };
#ifdef MACOSX
    constexpr int offset = 8;
#else
    constexpr int offset = 7;
#endif
    for (int i = offset; i != backtraceState->nDepth; ++i)
    {
        if (frameData[i].file.empty())
            continue;
        if (i != offset)
            b3.append('\n');
        b3.append('#');
        b3.append(i - offset);
        b3.append(' ');

        if (!frameData[i].info.isEmpty())
            b3.append(o3tl::runtimeToOUString(frameData[i].info.getStr()));
    }
    return b3.makeStringAndClear();
}

#else

OUString sal::backtrace_to_string(BacktraceState* backtraceState)
{
    std::unique_ptr<char*, decltype(free)*> b2{
        backtrace_symbols(backtraceState->buffer, backtraceState->nDepth), free
    };
    if (!b2)
    {
        return OUString();
    }
    OUStringBuffer b3;
    for (int i = 3; i != backtraceState->nDepth; ++i)
    {
        if (i != 0)
        {
            b3.append("\n");
        }
        b3.append("#" + OUString::number(i - 3) + " ");
        o3tl::runtimeToOUString(b2.get()[i]);

        b3.append(o3tl::runtimeToOUString(b2.get()[i]));
    }
    return b3.makeStringAndClear();
}

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
