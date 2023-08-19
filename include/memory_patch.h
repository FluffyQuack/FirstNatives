#pragma once
#include <string>
#include <vector>

typedef unsigned char byte;

const char* Rise = "MonsterHunterRise.exe";
const char* World = "MonsterHunterWorld.exe";

//std::vector<unsigned char*> aob(const std::string& str, const char* GameName);
std::vector<unsigned char*> aob(const std::string& str, const char *functionName);

std::vector<unsigned char> parseHex(const std::string& aob);

std::tuple<std::vector<unsigned char>, std::vector<unsigned char>> parseBinary(
    const std::string& aob);
