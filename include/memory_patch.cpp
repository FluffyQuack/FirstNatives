#pragma once

#include "memory_patch.h"

#include <Windows.h>
#include <Psapi.h>

#include <algorithm>
#include <functional>
#include <string>
#include <vector>
#include "reframework/API.hpp"
#include "..\dllmain.hpp"

using namespace reframework;

bool pred(byte b1, byte b2)
{
    if (b1 == b2)return true;
    if (b2 == '?')return true;
    return false;
}
std::vector<byte*> aob(const std::string& str, const char *functionName)
{
    std::vector<byte> bytes = parseHex(str);
    std::vector<byte*> results;
    auto module = GetModuleHandle(0);
    if (module == nullptr) return results;

    MODULEINFO moduleInfo;
    if (!GetModuleInformation(GetCurrentProcess(), module, &moduleInfo,
        sizeof(moduleInfo)))
        return results;

    byte* startAddr = (byte*)module;
    byte* endAddr = startAddr + moduleInfo.SizeOfImage;
    byte* addr = startAddr;

    while (addr < endAddr) {
        MEMORY_BASIC_INFORMATION memInfo;
        if (!VirtualQuery(addr, &memInfo, sizeof(memInfo)) ||
            memInfo.State != MEM_COMMIT || (memInfo.Protect & PAGE_GUARD))
            continue;
        byte* begin = (byte*)memInfo.BaseAddress;
        byte* end = begin + memInfo.RegionSize;

        byte* found = std::search(begin, end, bytes.begin(), bytes.end(), pred);
        while (found != end) {
            results.push_back(found);
            found = std::search(found + 1, end, bytes.begin(), bytes.end(), pred);
        }

        addr = end;
        memInfo = {};
    }

    //If AOB was found, then output the results to a log
    if (results.size() == 1 && logFile)
    {
        fprintf(logFile, "Found AOB %s for %s at %llu\r\n", str.c_str(), functionName, (ULONG64) results[0]);
    }

    return results;
}
/*
std::vector<byte*> aob(const std::string& str, const char* GameName)
{
    std::vector<byte> bytes = parseHex(str);
    std::vector<byte*> results;
    auto module = GetModuleHandle(GameName);
    if (module == nullptr) return results;

    MODULEINFO moduleInfo;
    if (!GetModuleInformation(GetCurrentProcess(), module, &moduleInfo,
        sizeof(moduleInfo)))
        return results;

    byte* startAddr = (byte*)module;
    byte* endAddr = startAddr + moduleInfo.SizeOfImage;
    byte* addr = startAddr;

    while (addr < endAddr) {
        MEMORY_BASIC_INFORMATION memInfo;
        if (!VirtualQuery(addr, &memInfo, sizeof(memInfo)) ||
            memInfo.State != MEM_COMMIT || (memInfo.Protect & PAGE_GUARD))
            continue;
        byte* begin = (byte*)memInfo.BaseAddress;
        byte* end = begin + memInfo.RegionSize;

        byte* found = std::search(begin, end, bytes.begin(), bytes.end(), pred);
        while (found != end) {
            results.push_back(found);
            found = std::search(found + 1, end, bytes.begin(), bytes.end(), pred);
        }

        addr = end;
        memInfo = {};
    }

    return results;
}
*/
std::vector<byte*> scanmem(const std::vector<byte>& bytes) {
    std::vector<byte*> results;
    auto module = GetModuleHandleA("MHRiseSunbreakDemo.exe");
    if (module == nullptr) return results;

    MODULEINFO moduleInfo;
    if (!GetModuleInformation(GetCurrentProcess(), module, &moduleInfo,
        sizeof(moduleInfo)))
        return results;

    byte* startAddr = (byte*)module;
    byte* endAddr = startAddr + moduleInfo.SizeOfImage;
    byte* addr = startAddr;

    while (addr < endAddr) {
        MEMORY_BASIC_INFORMATION memInfo;
        if (!VirtualQuery(addr, &memInfo, sizeof(memInfo)) ||
            memInfo.State != MEM_COMMIT || (memInfo.Protect & PAGE_GUARD))
            continue;
        byte* begin = (byte*)memInfo.BaseAddress;
        byte* end = begin + memInfo.RegionSize;

        byte* found = std::search(begin, end, bytes.begin(), bytes.end());
        while (found != end) {
            results.push_back(found);
            found = std::search(found + 1, end, bytes.begin(), bytes.end());
        }

        addr = end;
        memInfo = {};
    }

    return results;
}

std::vector<byte*> scanmem(const std::vector<byte>& bytes,
    const std::vector<byte>& mask) {
    std::vector<byte*> results;
    auto module = GetModuleHandleA("MHRiseSunbreakDemo.exe");
    if (module == nullptr) return results;

    MODULEINFO moduleInfo;
    if (!GetModuleInformation(GetCurrentProcess(), module, &moduleInfo,
        sizeof(moduleInfo)))
        return results;

    byte* startAddr = (byte*)module;
    byte* endAddr = startAddr + moduleInfo.SizeOfImage;
    byte* addr = startAddr;

    std::vector<std::tuple<byte, byte>> maskedBytes(bytes.size());
    for (int i = 0; i < bytes.size(); ++i) {
        maskedBytes[i] = { bytes[i], mask[i] };
    }

    while (addr < endAddr) {
        MEMORY_BASIC_INFORMATION memInfo;
        if (!VirtualQuery(addr, &memInfo, sizeof(memInfo)) ||
            memInfo.State != MEM_COMMIT || (memInfo.Protect & PAGE_GUARD))
            continue;
        byte* begin = (byte*)memInfo.BaseAddress;
        byte* end = begin + memInfo.RegionSize;

        auto predicate = [](byte a, std::tuple<byte, byte> b) {
            auto [check, mask] = b;
            return (a & mask) == (check & mask);
        };

        byte* found = std::search(begin, end, maskedBytes.begin(),
            maskedBytes.end(), predicate);

        while (found != end) {
            results.push_back(found);
            found = std::search(found + 1, end, maskedBytes.begin(),
                maskedBytes.end(), predicate);
        }

        addr = end;
        memInfo = {};
    }

    return results;
}

std::vector<byte> parseHex(const std::string& aob) {
    const std::string hexChars = "0123456789abcdef";
    std::string currentByte;
    std::vector<byte> ret;
    for (char c : aob) {
        if (hexChars.find(std::tolower(c)) == hexChars.npos)
        {
            if (c == '?')
            {
                ret.push_back('?');
                currentByte = "";
            }
            continue;
        }
        currentByte.push_back(c);
        if (currentByte.size() >= 2) {
            ret.push_back(std::stoi(currentByte, nullptr, 16));
            currentByte = "";
        }
    }
    return ret;
}

std::tuple<std::vector<byte>, std::vector<byte>> parseBinary(
    const std::string& aob) {
    std::vector<byte> data;
    std::vector<byte> mask;

    byte currentByte = 0;
    byte currentMask = 0;
    int bitsRead = 0;
    for (char c : aob) {
        if (std::string("01.").find(c) == std::string::npos) continue;
        bitsRead += 1;
        currentByte = (currentByte << 1) + (c == '1');
        currentMask = (currentMask << 1) + (c != '.');
        if (bitsRead == 8) {
            bitsRead = 0;
            data.push_back(currentByte);
            mask.push_back(currentMask);
            currentByte = 0;
            currentMask = 0;
        }
    }
    return { data, mask };
}

bool unprotect(void* ptr, size_t len, PDWORD oldp) {
    return VirtualProtect((LPVOID)(ptr), len, PAGE_EXECUTE_READWRITE, oldp);
}
bool protect(void* ptr, size_t len, PDWORD oldp) {
    DWORD dummy;
    return VirtualProtect((LPVOID)(ptr), len, *oldp, &dummy);
}
