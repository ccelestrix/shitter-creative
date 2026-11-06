#pragma once

#include <windows.h>
#include <tlhelp32.h>
#include <set>
#include <vector>
#include <algorithm>

#include <iostream>
#include <format>

// #define Log(...) std::cout << std::format(__VA_ARGS__) << '\n';

#include <vector>
#include <spdlog/spdlog.h>

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <filesystem>
#include <mutex>
#include <Windows.h>

#define ENABLE_SPD_LOG

static inline std::vector<spdlog::sink_ptr> sinks;

enum ELogLevel : uint8_t
{
    Trace,
    Debug,
    Info,
    Warning,
    Error,
    Critical,
    Disabled,
    All = Trace
};

// The folder XENBONO.dll was loaded from, with a trailing slash. Logs go there, so they are always in the same place
// whatever the game was started from. Falls back to the current directory.
inline std::string LogDirectory()
{
    HMODULE module = nullptr;
    char path[MAX_PATH] = {};

    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)&LogDirectory, &module) && GetModuleFileNameA(module, path, MAX_PATH))
    {
        std::string full = path;
        auto slash = full.find_last_of("\\/");

        if (slash != std::string::npos)
            return full.substr(0, slash + 1);
    }

    return "";
}

// ---------------------------------------------------------------------------------------------------------------------
// ForceLog: every log line is also written straight to a file with plain Windows calls (no spdlog, no buffering), so it
// still works when spdlog failed to start, and the line is on disk even if the game crashes right after it.
//   xenbono.log           every line from every logger
//   xenbono_creative.log  only the Creative lines (LogCreative)
// They go in the game's working directory. If that folder cannot be written to, the folder of the game executable, the
// folder of XENBONO.dll and the temp folder are tried, in that order. The folder that was used is logged and printed.
// ---------------------------------------------------------------------------------------------------------------------
namespace ForceLog
{
    inline std::mutex Lock;
    inline HANDLE AllFile = INVALID_HANDLE_VALUE;
    inline HANDLE CreativeFile = INVALID_HANDLE_VALUE;
    inline bool Tried = false;
    inline std::string Directory;

    inline std::string DirectoryOf(const std::string& path)
    {
        auto slash = path.find_last_of("\\/");
        return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
    }

    inline HANDLE OpenFile(const std::string& path)
    {
        return CreateFileA(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    }

    inline void WriteRaw(HANDLE file, const std::string& text)
    {
        if (file == INVALID_HANDLE_VALUE || text.empty())
            return;

        DWORD written = 0;
        WriteFile(file, text.data(), (DWORD)text.size(), &written, nullptr);
    }

    // Opens the files once. Callers hold Lock.
    inline void EnsureOpen()
    {
        if (Tried)
            return;

        Tried = true;

        char cwd[MAX_PATH] = {};
        GetCurrentDirectoryA(MAX_PATH, cwd);
        char exe[MAX_PATH] = {};
        GetModuleFileNameA(nullptr, exe, MAX_PATH);
        char temp[MAX_PATH] = {};
        GetTempPathA(MAX_PATH, temp);

        std::vector<std::string> candidates;
        candidates.push_back(std::string(cwd) + "\\");
        candidates.push_back(DirectoryOf(exe));
        candidates.push_back(LogDirectory());
        candidates.push_back(temp);

        for (auto& dir : candidates)
        {
            if (dir.empty())
                continue;

            AllFile = OpenFile(dir + "xenbono.log");

            if (AllFile == INVALID_HANDLE_VALUE)
                continue;

            CreativeFile = OpenFile(dir + "xenbono_creative.log");
            Directory = dir;
            break;
        }

        if (AllFile == INVALID_HANDLE_VALUE)
        {
            OutputDebugStringA("XENBONO: could not open a log file in any folder!\n");
            return;
        }

        char pid[32];
        sprintf_s(pid, "%lu", GetCurrentProcessId());
        std::string banner = std::string("XENBONO log started. Process ") + exe + " (pid " + pid + "), working directory " + cwd + ", logging to " + Directory + "\r\n";
        WriteRaw(AllFile, banner);
        WriteRaw(CreativeFile, banner);
        OutputDebugStringA(banner.c_str());
    }

    inline void Write(const char* level, const char* loggerName, const std::string& message)
    {
        SYSTEMTIME now;
        GetLocalTime(&now);

        char stamp[64];
        sprintf_s(stamp, "[%02d/%02d/%02d-%02d:%02d:%02d.%03d] ", now.wMonth, now.wDay, now.wYear % 100, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);

        std::string line = std::string(stamp) + loggerName + ": " + level + ": " + message + "\r\n";

        std::lock_guard<std::mutex> guard(Lock);
        EnsureOpen();
        WriteRaw(AllFile, line);

        if (strcmp(loggerName, "LogCreative") == 0)
            WriteRaw(CreativeFile, line);
    }

    inline void Write(const char* level, const char* loggerName, const char* message)
    {
        Write(level, loggerName, std::string(message));
    }

    // Called first thing when the DLL is loaded, so the files exist (and say where they are) even if nothing else runs.
    inline void Start()
    {
        std::lock_guard<std::mutex> guard(Lock);
        EnsureOpen();
    }

    // Writes what killed the game, if anything does. Only the codes that mean a crash; C++ exceptions and debugger
    // breakpoints that the game throws on purpose are skipped.
    inline LONG CALLBACK CrashHandler(PEXCEPTION_POINTERS info)
    {
        const DWORD code = info->ExceptionRecord->ExceptionCode;

        // Every error-severity code (0xC000xxxx: access violation, stack overflow, heap corruption, fast fail, ...). 0xC0000008 and 0xC0000024
        // are raised on purpose by the system/debugger paths and are skipped.
        const bool bFatal = (code & 0xF0000000) == 0xC0000000 && code != 0xC0000008 && code != 0xC0000024;

        if (!bFatal)
            return EXCEPTION_CONTINUE_SEARCH;

        const auto address = (unsigned long long)info->ExceptionRecord->ExceptionAddress;
        const auto base = (unsigned long long)GetModuleHandleW(nullptr);

        char text[256];
        sprintf_s(text, "Exception 0x%08lX at 0x%llX (game base 0x%llX, offset 0x%llX), thread %lu", code, address, base, address - base, GetCurrentThreadId());
        Write("critical", "LogCrash", text);
        return EXCEPTION_CONTINUE_SEARCH;
    }

    // Every thread created in this process after the hook was installed: who started it, when, and where it runs. A thread that ends the process
    // can then be traced back to whatever created it.
    struct ThreadRecord
    {
        DWORD Id;
        ULONGLONG Tick;
        void* Start;
        void* Frames[5];
    };

    inline ThreadRecord ThreadRing[256] = {};
    inline volatile LONG ThreadRingNext = 0;

    inline void NoteThread(DWORD id, void* start)
    {
        ThreadRecord record{};
        record.Id = id;
        record.Tick = GetTickCount64();
        record.Start = start;
        CaptureStackBackTrace(2, 5, record.Frames, nullptr);
        ThreadRing[(unsigned)(InterlockedIncrement(&ThreadRingNext) - 1) % 256] = record;
    }

    inline std::string AddressText(void* address)
    {
        HMODULE module = nullptr;
        char path[MAX_PATH] = {};

        if (address && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)address, &module) && module)
            GetModuleFileNameA(module, path, MAX_PATH);

        const char* name = path[0] ? strrchr(path, 92) : nullptr;
        char text[MAX_PATH + 40];
        sprintf_s(text, "%s + 0x%llX", name ? name + 1 : (path[0] ? path : "unknown"), (unsigned long long)address - (unsigned long long)module);
        return text;
    }

    inline void LogThreads(DWORD self, int lastCount)
    {
        const LONG next = ThreadRingNext;
        const ULONGLONG now = GetTickCount64();
        char text[1024];

        for (LONG back = 1; back <= (std::min)(next, (LONG)256); ++back)
        {
            const ThreadRecord& record = ThreadRing[(unsigned)(next - back) % 256];

            if (record.Id != self && back > lastCount)
                continue;

            sprintf_s(text, "  thread %lu%s, created %llu ms ago, runs %s, created through %s | %s | %s | %s", record.Id, record.Id == self ? " (THE ONE THAT ENDED THE PROCESS)" : "", now - record.Tick,
                AddressText(record.Start).c_str(), AddressText(record.Frames[0]).c_str(), AddressText(record.Frames[1]).c_str(), AddressText(record.Frames[2]).c_str(), AddressText(record.Frames[3]).c_str());
            Write("critical", "LogCrash", text);
        }
    }

    // The modules loaded in the process (names only), written when asked and compared with the first list when the process ends.
    inline std::set<std::string>& StartupModules()
    {
        static std::set<std::string> names;
        return names;
    }

    inline std::vector<std::string> CurrentModules()
    {
        std::vector<std::string> names;
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());

        if (snapshot == INVALID_HANDLE_VALUE)
            return names;

        MODULEENTRY32W entry{};
        entry.dwSize = sizeof(entry);

        for (BOOL ok = Module32FirstW(snapshot, &entry); ok; ok = Module32NextW(snapshot, &entry))
        {
            std::string name;

            for (const wchar_t* c = entry.szModule; *c; ++c)
                name += (*c < 128) ? (char)*c : '?';

            names.push_back(name);
        }

        CloseHandle(snapshot);
        return names;
    }

    inline void LogModules(const char* when)
    {
        auto names = CurrentModules();
        std::string line;

        for (auto& name : names)
        {
            StartupModules().insert(name);
            line += name + ", ";
        }

        char text[64];
        sprintf_s(text, "Modules (%s): %d", when, (int)names.size());
        Write("info", "LogInit", text);
        Write("info", "LogInit", line);
    }

    // Called when the game (or something in it) ends the process, so a death with no exception still leaves a line. Says which module called
    // it and where, so it can be looked up.
    inline void LogExit(const char* What, UINT code, void* returnAddress)
    {
        HMODULE module = nullptr;
        char path[MAX_PATH] = {};

        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)returnAddress, &module) && module)
            GetModuleFileNameA(module, path, MAX_PATH);

        char text[512];
        sprintf_s(text, "%s(%u) called from %s + 0x%llX (thread %lu)", What, code, path[0] ? path : "an unknown module", (unsigned long long)returnAddress - (unsigned long long)module, GetCurrentThreadId());
        Write("critical", "LogCrash", text);

        // The whole call chain that led here, every frame as module + offset, so it can be seen whether the game or this DLL started the exit.
        void* frames[24] = {};
        const USHORT count = CaptureStackBackTrace(0, 24, frames, nullptr);

        for (USHORT i = 0; i < count; ++i)
        {
            HMODULE frameModule = nullptr;
            char framePath[MAX_PATH] = {};

            if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)frames[i], &frameModule) && frameModule)
                GetModuleFileNameA(frameModule, framePath, MAX_PATH);

            const char* name = framePath[0] ? strrchr(framePath, 92) : nullptr;
            sprintf_s(text, "  frame %u: %s + 0x%llX", (unsigned)i, name ? name + 1 : (framePath[0] ? framePath : "unknown"), (unsigned long long)frames[i] - (unsigned long long)frameModule);
            Write("critical", "LogCrash", text);
        }

        LogThreads(GetCurrentThreadId(), 12);

        std::string added;

        for (auto& name : CurrentModules())
            if (!StartupModules().count(name))
                added += name + ", ";

        Write("critical", "LogCrash", "  modules loaded since the first list: " + (added.empty() ? std::string("none") : added));
    }

    inline void InstallCrashLogging()
    {
        AddVectoredExceptionHandler(1, CrashHandler);
    }
}

inline void MakeLogger(const std::string& LoggerName)
{
    auto logger = std::make_shared<spdlog::logger>(LoggerName, sinks.begin(), sinks.end());
    spdlog::register_logger(logger);

    logger->set_level(spdlog::level::level_enum::info);
    logger->flush_on(spdlog::level::level_enum::info);
}

inline void InitLogger()
{
    ForceLog::Start();
    ForceLog::InstallCrashLogging();

    // FreeConsole();
    // AttachConsole(ATTACH_PARENT_PROCESS);

    FILE* stream = nullptr;

    bool bStopFortniteOutput = false;

    if (bStopFortniteOutput)
    {
        AllocConsole();

        freopen_s(&stream, "in.txt", "r", stdin);
        freopen_s(&stream, "out.txt", "w+", stdout);
        freopen_s(&stream, "err.txt", "w", stderr);
    }

    SetConsoleTitleA("XENBONO");
    
#ifdef ENABLE_SPD_LOG

    sinks.emplace_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>())->set_pattern("[%D-%T] %n: %^%v%$");

    MakeLogger("LogTeams");
    MakeLogger("LogMemory");
    MakeLogger("LogFinder");
    MakeLogger("LogInit");
    MakeLogger("LogNet");
    MakeLogger("LogDev");
    MakeLogger("LogPlayer");
    MakeLogger("LogLoot");
    MakeLogger("LogMinigame");
    MakeLogger("LogLoading");
    MakeLogger("LogHook");
    MakeLogger("LogAbilities");
    MakeLogger("LogEvent");
    MakeLogger("LogPlaylist");
    MakeLogger("LogGame");
    MakeLogger("LogAI");
    MakeLogger("LogInteraction");
    MakeLogger("LogCreative"); // its lines also go to xenbono_creative.log, see ForceLog
    MakeLogger("LogZone");
    MakeLogger("LogReplication");
    MakeLogger("LogMutator");
    MakeLogger("LogVehicles");
    MakeLogger("LogUI");
    MakeLogger("LogBots");
    MakeLogger("LogCosmetics");
    MakeLogger("LogMatchmaker");
    MakeLogger("LogRebooting");
    MakeLogger("LogObjectViewer");
    MakeLogger("LogLateGame");

    std::cout << "XENBONO logs: " << ForceLog::Directory << "xenbono.log and xenbono_creative.log\n";
#endif
}

#define XENBONO_LOG(level, method, loggerName, ...)                                       \
    do                                                                                    \
    {                                                                                     \
        const std::string XenbonoLogMessage = std::format(__VA_ARGS__);                   \
        ForceLog::Write(level, #loggerName, XenbonoLogMessage);                           \
        if (auto XenbonoLogger = spdlog::get(#loggerName))                                \
            XenbonoLogger->method(XenbonoLogMessage);                                     \
    } while (0); /* the old macros ended in a semicolon and some call sites rely on it */

#define LOG_DEBUG(loggerName, ...) XENBONO_LOG("debug", debug, loggerName, __VA_ARGS__)
#define LOG_INFO(loggerName, ...) XENBONO_LOG("info", info, loggerName, __VA_ARGS__)
#define LOG_WARN(loggerName, ...) XENBONO_LOG("warn", warn, loggerName, __VA_ARGS__)
#define LOG_ERROR(loggerName, ...) XENBONO_LOG("error", error, loggerName, __VA_ARGS__)
#define LOG_FATAL(loggerName, ...) XENBONO_LOG("critical", critical, loggerName, __VA_ARGS__)

#define DEBUG_LOG_INFO(loggerName, ...) XENBONO_LOG("debug", debug, loggerName, __VA_ARGS__)