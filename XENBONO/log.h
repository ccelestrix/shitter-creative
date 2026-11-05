#pragma once

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

        const bool bFatal = code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_STACK_OVERFLOW || code == EXCEPTION_ILLEGAL_INSTRUCTION || code == EXCEPTION_INT_DIVIDE_BY_ZERO
            || code == EXCEPTION_IN_PAGE_ERROR || code == EXCEPTION_PRIV_INSTRUCTION || code == EXCEPTION_ARRAY_BOUNDS_EXCEEDED || code == 0xC0000374 /* heap corruption */;

        if (!bFatal)
            return EXCEPTION_CONTINUE_SEARCH;

        const auto address = (unsigned long long)info->ExceptionRecord->ExceptionAddress;
        const auto base = (unsigned long long)GetModuleHandleW(nullptr);

        char text[256];
        sprintf_s(text, "Exception 0x%08lX at 0x%llX (game base 0x%llX, offset 0x%llX), thread %lu", code, address, base, address - base, GetCurrentThreadId());
        Write("critical", "LogCrash", text);
        return EXCEPTION_CONTINUE_SEARCH;
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