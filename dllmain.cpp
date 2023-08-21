// dllmain.cpp : 定义 DLL 应用程序的入口点。
#include "windows.h"
#include <fstream>
#include <map>
#include <string>
#include <thread>
#include <filesystem>
#include "util.h"
#include "memory_patch.cpp"
#include "reframework/API.hpp"

using namespace std;
using namespace reframework;

void* (*loadFile)(void*, wchar_t*, int) = (void* (*)(void*, wchar_t*, int))0; //MonsterHunterRise.exe+3D536B0
void* (*_loadFile)(void*, wchar_t*, int) = (void* (*)(void*, wchar_t*, int))0; //MonsterHunterRise.exe+3E57D40 
int (*CheckFileInPak)(void*, UINT64) = (int (*)(void*, UINT64))0; //MHRiseSunbreakDemo.exe+3057E80
UINT64(*PathToHash)(wchar_t*) = (ULONG64(*)(wchar_t*))0; //MHRiseSunbreakDemo.exe+3058DF0
int (*LookForPathInPAK)(void *, wchar_t*) = (int (*)(void *, wchar_t*))0; //This is from DMC5, and later versions of the engine evolved this function into PathToHash
void(*_CloseHandle)(HANDLE*) = (void(*)(HANDLE*))0; //143bdcee0
bool(*prePath)(void*, wchar_t*, void*) = 0;//MonsterHunterRise.exe + 3D57430
void(*loadPfb)(void* rfb, bool t) = 0;//143E63700

//This function is called just after loadFile in MHR, but since I couldn't find loadFile in RE4R, I'm using this instead
void *(*loadFile_step2)(void *, wchar_t*, int) = 0; //14368C700 (first pointer looks to be a pointer to a struct)

//void snow.player.PlayerManager::reqPlayer(void* vmctx, snow.player.PlayerManager* this,snow.player.PlayerRequestEquipsData* equipsData)
void (*reqPlayer)(void* vmctx, void* self, void* equipsData);

void* (*findMasterPlayer)(void* vmctx, void* self) = 0;
//System.String * System.Enum::GetName(void *vmctx,System.Type *enumType,System.Object *value)

enum
{
    LOADMETHOD_NONE,
    LOADMETHOD_MHR, //Note path's hash if the path exists during loadFile and then return -1 during CheckFileInPak if it's been noted
    LOADMETHOD_MHR_ALT, //Same as above but we hook into loadFile_step2 rather than loadFile
    LOADMETHOD_DMC5, //Hook into LookForPathInPAK and return -1 if file path exists
    LOADMETHOD_MHR_ALT2, //Hook into PathToHash and return 0 if file path exists
};

map<UINT64, string> hashs;
bool bone = false;
map<int, string>enums;
string modelId;
FILE *logFile = 0;
bool mhrFunctionsFound = 0;
int loadMethod = LOADMETHOD_NONE;

void* get_method(string type, string name)
{
    return API::get()->tdb()->find_method(type, name)->get_function_raw();
}
void init_enum()
{
    if (enums.empty())
    {
        auto type = API::get()->tdb()->find_type("snow.data.DataDef.PlArmorModelId");
        auto fields = type->get_fields();
        for (auto iter = fields.begin(); iter != fields.end(); iter++)
        {
            auto field = *iter;
            if (field->is_static())
            {
                string name = field->get_name();
                int value = field->get_data<int>();
                enums[value] = name;
            }
        }
    }
}
string wideCharToMultiByte(wchar_t* pWCStrKey)
{
    int pSize = WideCharToMultiByte(CP_OEMCP, 0, pWCStrKey, wcslen(pWCStrKey), NULL, 0, NULL, NULL);
    char* pCStrKey = new char[pSize + 1];
    WideCharToMultiByte(CP_OEMCP, 0, pWCStrKey, wcslen(pWCStrKey), pCStrKey, pSize, NULL, NULL);
    pCStrKey[pSize] = '\0';
    string str = string(pCStrKey);
    delete[] pCStrKey;
    return str;
}
void multiByteToWideChar(const string& pKey, wchar_t* pWCStrKey)
{
    const char* pCStrKey = pKey.c_str();
    int pSize = MultiByteToWideChar(CP_OEMCP, 0, pCStrKey, strlen(pCStrKey) + 1, NULL, 0);
    MultiByteToWideChar(CP_OEMCP, 0, pCStrKey, strlen(pCStrKey) + 1, pWCStrKey, pSize);
}

void hook()
{
    MH_Initialize();

    if(loadMethod == LOADMETHOD_MHR_ALT && loadFile_step2) //RE4R
    {
        HookLambda(loadFile_step2, [](auto a, auto b, auto c) {
            if (b == nullptr)return original(a, b, c);
            string path = wideCharToMultiByte(b);
            UINT64 hash = PathToHash(b);

            if(logFile)
            {
                fprintf(logFile, "%s\r\n", path.c_str());
                fflush(logFile);
            }

            if (filesystem::exists(path.c_str()))
            {
                hashs[hash] = path;
            }
            return original(a, b, c);
            });
    }
    else if(loadMethod == LOADMETHOD_MHR && loadFile) //MHR and others
    {
        HookLambda(loadFile, [](auto a, auto b, auto c) {
            if (b == nullptr)return original(a, b, c);
            string path = wideCharToMultiByte(b);
            if(mhrFunctionsFound && reqPlayer)
            {
                auto f = path.find("_shadow.mesh.2109148288", 0);
                if (f != string::npos)
                {
                    auto bone_index = path.find("bone", 0);
                    if (bone_index != string::npos && bone)
                    {
                        string npath = path.replace(bone_index, 4, modelId);
                        if (filesystem::exists(npath.c_str()))
                        {
                            path = npath;
                            multiByteToWideChar(path, b);
                        }
                    }
                }
            }
            UINT64 hash = PathToHash(b);

            if(logFile)
            {
                fprintf(logFile, "%s\r\n", path.c_str());
                fflush(logFile);
            }

            if (filesystem::exists(path.c_str()))
            {
                hashs[hash] = path;
            }
            return original(a, b, c);
            });
    }
    else if(loadMethod == LOADMETHOD_DMC5 && LookForPathInPAK) //DMC5
    {
        HookLambda(LookForPathInPAK, [](auto a, auto b) {
            int returnValue = original(a, b);
            if(returnValue != -1 && b != 0 && b[0] != 0 && filesystem::exists(b))
                return -1;
            return returnValue;
            });
    }
    else if(loadMethod == LOADMETHOD_MHR_ALT2 && PathToHash)
    {
        HookLambda(PathToHash, [](wchar_t *a) {
            ULONG64 returnValue = original(a);
            if(a != 0 && a[0] != 0 && filesystem::exists(a))
                returnValue = 0;
            return returnValue;
            });
    }
    
    if((loadMethod == LOADMETHOD_MHR || loadMethod == LOADMETHOD_MHR_ALT) && CheckFileInPak)
    {
        HookLambda(CheckFileInPak, [](auto a, auto b) { //判断hash是否存在pak中
            int ret = original(a, b);
            if (ret == -1)return ret;
            if (hashs.find(b) != hashs.end())
            {
                if (filesystem::exists(hashs[b]))
                {
                    ret = -1;
                }
                else
                {
                    hashs.erase(b);
                }
            }
            return ret;
            });
    }

    if(mhrFunctionsFound && reqPlayer) //MHR function
    {
        HookLambda(reqPlayer, [](void* vmctx, void* self, void* equipsData) {
            int* index = offsetPtr<int>(*offsetPtr<void*>(equipsData, 0x10), 0x24);
            bool suit = true;
            for (int i = 0; i < 4; i++)
            {
                int temp = *index++;
                suit = suit && (temp == *index);
            }
            bone = suit;
            if (bone)
            {
                init_enum();
                modelId = enums[*index];
            }
            return original(vmctx, self, equipsData);
            }
        );
    }
    MH_ApplyQueued();
}

bool Aob()
{
    vector<BYTE*> ret;
    if(PathToHash == 0)
    {
        ret = aob("40 55 53 41 56 48 8d ac 24 c0 f0 ff ff", "PathToHash"); //MHR / RE4 / RE8 / RE7-RT / RE2R-RT / RE3R-RT / SF6
        if(ret.size() == 1)
            PathToHash = (ULONG64(*)(wchar_t*))(ret[0]);
    }
    if(LookForPathInPAK == 0)
    {
        ret = aob("40 55 41 54 41 55 41 57 48 8D AC 24 C8 F0 FF FF", "CheckFileInPak"); //DMC5
        if(ret.size() == 1)
            LookForPathInPAK = (int (*)(void *, wchar_t*))(ret[0]);
    }
    if(CheckFileInPak == 0)
    {
        ret = aob("48 89 6C 24 20 41 56 48 83 EC 20 48 83 B9 A8 00 00 00 00", "CheckFileInPak"); //RE4 / RE8
        if(ret.size() != 1)
            ret = aob("48 89 6C 24 20 41 56 48 83 EC 20 45 33 C0", "CheckFileInPak"); //MHR / SF6
        if(ret.size() != 1)
            ret = aob("41 56 41 57 48 83 EC 28 48 83 B9 A8 00 00 00 00", "CheckFileInPak"); //RE7-RT / RE2-RT
        if(ret.size() == 1)
            CheckFileInPak = (int (*)(void*, UINT64))(ret[0]);
    }
    if(loadFile == 0)
    {
        ret = aob("40 53 48 83 EC 20 48 8B D9 E8 ? ? ? ? 48 8D 05 ? ? ? ? 48 89 03 33 C0 48 89 83 98 00 00 00 48 89 83 A0 00 00 00 48 89 83 A8 00 00 00 48 8B C3 48 83 C4 20 5B C3", "loadFile"); //MHR / RE8 / RE7-RT / RE2-RT / RE3-RT
        if(ret.size() == 1)
            loadFile = (void* (*)(void*, wchar_t*, int))(ret[0]); //MonsterHunterRise.exe+3B2AF60 
    }
    if(_CloseHandle == 0)
    {
        ret = aob("40 53 48 83 EC 20 48 8B D9 E8 ? ? ? ? 48 8B 4B 50 48 85 c9 74 0E FF 15 ? ? ? ? 48 C7 43 50 00 00 00 00 48 83 C4 20 5B C3", "_CloseHandle"); //MHR / RE4 / RE8 / RE7-RT / RE2-RT / RE3-RT / SF6 / RE2 / RE3 / DMC5
        if(ret.size() == 1)
            _CloseHandle = (void (*)(HANDLE*))(ret[0]);
    }
    if(reqPlayer == 0)
    {
        ret = aob("4C 8B DC 49 89 5B 08 55 56 57 41 54 41 55 41 56 41 57 48 83 EC 50 48 8B 42 50 49 8B F0 4C 8B FA 48 89 44 24 40 48 8B D0 41 C6 43 18 00 4D 8D 43 18 48 8B D9 E8", "reqPlayer"); //MHR
        if(ret.size() == 1)
            reqPlayer = (void (*)(void*, void*, void*))(ret[0]);
    }
    if(loadFile_step2 == 0)
    {
        ret = aob("48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 48 83 EC 20 48 8B F1 44 89 41 08", "loadFile_step2"); //RE4 / RE3-NONRT / DMC5
        if(ret.size() == 1)
            loadFile_step2 = (void* (*)(void *, wchar_t*, int))(ret[0]);
    }
    
    if(PathToHash && CheckFileInPak && loadFile && _CloseHandle && reqPlayer)
    {
        mhrFunctionsFound = 1;
        loadMethod = LOADMETHOD_MHR;
        if(logFile)
            fprintf(logFile, "Found functions for MHR load method with additional MHR functions\r\n");
    }
    else if(PathToHash)
    {
        loadMethod = LOADMETHOD_MHR_ALT2;
        if(logFile)
            fprintf(logFile, "Found function for MHR alt2 load method\r\n");
    }
    else if(PathToHash && CheckFileInPak && loadFile)
    {
        loadMethod = LOADMETHOD_MHR;
        if(logFile)
            fprintf(logFile, "Found functions for MHR load method\r\n");
    }
    else if(PathToHash && CheckFileInPak && loadFile_step2) //RE4R
    {
        loadMethod = LOADMETHOD_MHR_ALT;
        if(logFile)
            fprintf(logFile, "Found functions for MHR alt load method\r\n");
    }
    else if(LookForPathInPAK)
    {
        loadMethod = LOADMETHOD_DMC5;
        if(logFile)
            fprintf(logFile, "Found function for DMC5 load method\r\n");
    }

    if(logFile)
        fflush(logFile);

    if(loadMethod != LOADMETHOD_NONE)
        return true;
    return false;
}
void Init()
{
    //Debug log file
    fopen_s(&logFile, "FirstNatives.log", "wb");

    while (true)
    {
        if (Aob())
        {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
    hook();
}
bool Load()
{
    thread(Init).detach();
    return true;
}
extern "C" __declspec(dllexport) void reframework_plugin_required_version(REFrameworkPluginVersion * version) {
    version->major = REFRAMEWORK_PLUGIN_VERSION_MAJOR;
    version->minor = REFRAMEWORK_PLUGIN_VERSION_MINOR;
    version->patch = REFRAMEWORK_PLUGIN_VERSION_PATCH;
}

extern "C" __declspec(dllexport) bool reframework_plugin_initialize(const REFrameworkPluginInitializeParam * param) {
    reframework::API::initialize(param);
    return true;
}
BOOL APIENTRY DllMain(HMODULE hModule,
    DWORD  ul_reason_for_call,
    LPVOID lpReserved
)
{
    if (ul_reason_for_call == DLL_PROCESS_ATTACH)
        return Load();
    return TRUE;
}