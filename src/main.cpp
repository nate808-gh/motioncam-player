// FILE: src/main.cpp
#include <QDir>
#include <iostream>
#include <string>
#include <filesystem>
#include <stdexcept>
#include <fstream>
#include <chrono>
#include <ctime>
#include <cstring>
#include <iomanip>
#include <sstream>

#ifdef _WIN32
#   define WIN32_LEAN_AND_MEAN
#   include <windows.h>
#   include <commdlg.h>
#   include <shellapi.h>
#   pragma comment(lib, "comdlg32.lib")
#   include <stdio.h>
#   include <fcntl.h>
#   include <io.h>
#endif

#include <SDL.h>

#include "App/App.h"
#include "Utils/DebugLog.h"
#ifdef _WIN32
#include "Utils/SingleInstanceGuard.h"
#ifdef _WIN32
namespace DebugLogHelper {
    extern std::string wstring_to_utf8(const std::wstring& wstr);
}
#endif
#endif

namespace fs = std::filesystem;

#ifdef _WIN32
void RedirectIOToConsole() {
    LogToFile("[RedirectIOToConsole] Attempting to redirect IO to console.");
    bool consoleAttached = false;
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        consoleAttached = true;
        LogToFile("[RedirectIOToConsole] Attached to parent console.");
    }
    else if (AllocConsole()) {
        consoleAttached = true;
        LogToFile("[RedirectIOToConsole] Allocated new console.");
    }

    if (consoleAttached) {
        FILE* fp = nullptr;
        if (freopen_s(&fp, "CONOUT$", "w", stdout) == 0 && fp != nullptr) {
            setvbuf(stdout, NULL, _IONBF, 0);
        }
        else {
            LogToFile("[RedirectIOToConsole] Failed to redirect stdout.");
        }
        if (freopen_s(&fp, "CONIN$", "r", stdin) == 0 && fp != nullptr) {
            setvbuf(stdin, NULL, _IONBF, 0);
        }
        else {
            LogToFile("[RedirectIOToConsole] Failed to redirect stdin.");
        }
        if (freopen_s(&fp, "CONOUT$", "w", stderr) == 0 && fp != nullptr) {
            setvbuf(stderr, NULL, _IONBF, 0);
        }
        else {
            LogToFile("[RedirectIOToConsole] Failed to redirect stderr.");
        }
        std::ios::sync_with_stdio(true);
        std::cout << "[RedirectIOToConsole] Console IO redirection attempted." << std::endl;
        std::cerr << "[RedirectIOToConsole] Test: stderr output after redirection." << std::endl;
        std::cout.flush(); std::cerr.flush();
    }
    else {
        LogToFile("[RedirectIOToConsole] Failed to attach or allocate console.");
    }
}
#endif

static std::string OpenMcrawDialog() {
    LogToFile("[OpenMcrawDialog] Called.");
#ifdef _WIN32
    OPENFILENAMEW ofn{};
    wchar_t szFile[MAX_PATH] = { 0 };
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFilter = L"MotionCam RAW files\0*.mcraw\0All Files\0*.*\0";
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    ofn.lpstrDefExt = L"mcraw";

    if (GetOpenFileNameW(&ofn)) {
        std::string utf8Path = DebugLogHelper::wstring_to_utf8(szFile);
        if (utf8Path.empty() && szFile[0] != L'\0') {
            LogToFile("[OpenMcrawDialog] WideCharToMultiByte failed or returned empty. Error: " + std::to_string(GetLastError()));
            return {};
        }
        LogToFile(std::string("[OpenMcrawDialog] File selected: ") + utf8Path);
        return utf8Path;
    }
    LogToFile("[OpenMcrawDialog] Dialog cancelled or no file selected. GetLastError(): " + std::to_string(GetLastError()));
#else
    LogToFile("[OpenMcrawDialog] File dialog not implemented for this platform.");
    std::cerr << "File dialog not implemented. Please provide file as command line argument." << std::endl;
#endif
    return {};
}

int main(int argc, char* argv[]) {
    // Set the working directory to /opt/motioncam-fs-bin to ensure resources are found
    QDir::setCurrent("/opt/motioncam-fs-bin");

    try {
        LogToFile(std::string("[main] Current Working Directory: ") + fs::current_path().string());
    }
    catch (const fs::filesystem_error& e) {
        LogToFile(std::string("[main] Error getting CWD: ") + e.what());
    }

    LogToFile("--------------------------------------------------");
    LogToFile(std::string("[main] Continuing main() for primary instance. argc: ") + std::to_string(argc));
    if (argc > 0 && argv[0] != nullptr) {
        LogToFile(std::string("[main] argv[0]: ") + argv[0]);
    }
    else if (argc > 0) {
        LogToFile(std::string("[main] argv[0] is nullptr."));
    }

    std::string inPath;
    if (argc >= 2 && argv[1] != nullptr) {
        inPath = argv[1];
        LogToFile(std::string("[main] Input file from command line: ") + inPath);
    }
    else {
        LogToFile("[main] No command line argument provided or argv[1] is null, opening file dialog...");
        inPath = OpenMcrawDialog();
        if (inPath.empty()) {
            LogToFile("[main] No input file selected from dialog or dialog cancelled. Exiting.");
            return 0;
        }
        LogToFile(std::string("[main] Input file from dialog: ") + inPath);
    }

    if (!fs::exists(inPath) || !fs::is_regular_file(inPath)) {
        std::string errorMsg = "[main] Input file not found or not a regular file: " + inPath;
        LogToFile(errorMsg);
#ifdef _WIN32
        MessageBoxA(NULL, errorMsg.c_str(), "Error - MCRAW Player", MB_OK | MB_ICONERROR);
#endif
        std::cerr << errorMsg << std::endl;
        return 1;
    }
    if (fs::path(inPath).extension() != ".mcraw") {
        std::string errorMsg = "[main] Input file must have a .mcraw extension: " + inPath;
        LogToFile(errorMsg);
#ifdef _WIN32
        MessageBoxA(NULL, errorMsg.c_str(), "Error - MCRAW Player", MB_OK | MB_ICONERROR);
#endif
        std::cerr << errorMsg << std::endl;
        return 1;
    }

    LogToFile(std::string("[main] Initializing App with file: ") + inPath);
    try {
        App app(inPath);
        LogToFile("[main] App object created. Calling app.run()...");
        if (!app.run()) {
            LogToFile("[main] App::run() returned false. Application will exit.");
#ifdef _WIN32
            MessageBoxA(NULL, "Application run failed. See mcraw_player_debug_log.txt for details.", "Runtime Error - MCRAW Player", MB_OK | MB_ICONERROR);
#endif
            std::cerr << "[main] App::run() returned false. Application will exit." << std::endl;
            return 1;
        }
        LogToFile("[main] App::run() finished successfully.");
    }
    catch (const std::exception& e) {
        std::string errorMsg = "[main] FATAL STD EXCEPTION: " + std::string(e.what());
        LogToFile(errorMsg);
#ifdef _WIN32
        MessageBoxA(NULL, errorMsg.c_str(), "Runtime Error - MCRAW Player", MB_OK | MB_ICONERROR);
#endif
        std::cerr << errorMsg << std::endl;
        return 1;
    }
    catch (...) {
        std::string errorMsg = "[main] FATAL UNKNOWN EXCEPTION occurred.";
        LogToFile(errorMsg);
#ifdef _WIN32
        MessageBoxA(NULL, errorMsg.c_str(), "Runtime Error - MCRAW Player", MB_OK | MB_ICONERROR);
#endif
        std::cerr << errorMsg << std::endl;
        return 1;
    }

    LogToFile("[main] Application exiting normally (end of main).");
    return 0;
}
