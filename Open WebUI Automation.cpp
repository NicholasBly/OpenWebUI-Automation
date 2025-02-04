#include <windows.h>
#include <tlhelp32.h>
#include <winhttp.h>
#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#include <fstream>
#include <filesystem>
#include <vector>
#include <stdexcept>
#include <nlohmann/json.hpp>

#pragma comment(lib, "winhttp.lib")

using json = nlohmann::json;
namespace fs = std::filesystem;
using namespace std::chrono_literals;

// -------------------------
// Logging Helper
// -------------------------
enum class LogLevel { Info, Warning, Error };

void Log(LogLevel level, const std::wstring& message) {
    const std::wstring prefix =
        (level == LogLevel::Info) ? L"[INFO] " :
        (level == LogLevel::Warning) ? L"[WARNING] " : L"[ERROR] ";
    std::wcout << prefix << message << std::endl;
}

// -------------------------
// Console Manager
// -------------------------
class ConsoleManager {
public:
    static void Hide() { ::ShowWindow(::GetConsoleWindow(), SW_HIDE); }
    static void Show() { ::ShowWindow(::GetConsoleWindow(), SW_SHOW); }
};

// -------------------------
// Unicode conversion helper
// -------------------------
std::wstring UTF8ToWString(const std::string& str) {
    if (str.empty())
        return std::wstring();
    const int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.data(),
        static_cast<int>(str.size()), nullptr, 0);
    std::wstring wstr(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.data(),
        static_cast<int>(str.size()), &wstr[0], size_needed);
    return wstr;
}

// -------------------------
// Configuration structure
// -------------------------
struct Config {
    std::wstring ollamaPath;
    std::wstring dockerPath;

    bool isValid() const {
        return !ollamaPath.empty() && !dockerPath.empty();
    }
};

// -------------------------
// Process Manager
// -------------------------
class ProcessManager {
public:
    // Check if a process (by executable name) is running.
    static bool IsRunning(std::wstring_view processName) {
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE) return false;

        PROCESSENTRY32W pe32{ sizeof(pe32) };
        bool found = false;

        if (Process32FirstW(snapshot, &pe32)) {
            do {
                if (_wcsicmp(pe32.szExeFile, processName.data()) == 0) {
                    found = true;
                    break;
                }
            } while (Process32NextW(snapshot, &pe32));
        }
        CloseHandle(snapshot);
        return found;
    }

    // Wait for at least one process from a list to appear, up to a timeout.
    static bool WaitForAnyProcess(const std::vector<std::wstring>& processNames,
        std::chrono::milliseconds checkInterval = 500ms,
        std::chrono::milliseconds timeout = 10000ms)
    {
        const auto start = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - start < timeout) {
            for (const auto& name : processNames) {
                if (IsRunning(name)) {
                    Log(LogLevel::Info, L"Detected process: " + name);
                    return true;
                }
            }
            std::this_thread::sleep_for(checkInterval);
        }
        return false;
    }

    // Overload for waiting for a single process.
    static bool WaitForProcess(std::wstring_view processName,
        std::chrono::milliseconds checkInterval = 500ms,
        std::chrono::milliseconds timeout = 10000ms)
    {
        return WaitForAnyProcess({ std::wstring(processName) }, checkInterval, timeout);
    }

    // Start a process given its full path.
    static bool Start(const std::wstring& path) {
        STARTUPINFOW si{ sizeof(si) };
        PROCESS_INFORMATION pi{};
        BOOL success = CreateProcessW(path.c_str(), nullptr, nullptr, nullptr,
            FALSE, 0, nullptr, nullptr, &si, &pi);

        if (success) {
            Log(LogLevel::Info, L"Started process: " + path);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
        else {
            DWORD errorCode = GetLastError();
            Log(LogLevel::Error, L"Failed to start process: " + path +
                L" Error code: " + std::to_wstring(errorCode));
        }
        return success != 0;
    }

    // Kill all processes that match the given process name.
    static void Kill(std::wstring_view processName) {
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE) return;

        PROCESSENTRY32W pe32{ sizeof(pe32) };
        if (Process32FirstW(snapshot, &pe32)) {
            do {
                if (_wcsicmp(pe32.szExeFile, processName.data()) == 0) {
                    HANDLE process = OpenProcess(PROCESS_TERMINATE, FALSE, pe32.th32ProcessID);
                    if (process) {
                        if (TerminateProcess(process, 0))
                            Log(LogLevel::Info, L"Terminated process: " + std::wstring(processName));
                        else {
                            DWORD errorCode = GetLastError();
                            Log(LogLevel::Error, L"Failed to terminate process: " + std::wstring(processName) +
                                L" Error code: " + std::to_wstring(errorCode));
                        }
                        CloseHandle(process);
                    }
                }
            } while (Process32NextW(snapshot, &pe32));
        }
        CloseHandle(snapshot);
    }

    // Execute a command as administrator (using runas verb).
    static bool ExecuteAsAdmin(const std::wstring& command) {
        SHELLEXECUTEINFOW sei{ sizeof(sei) };
        sei.lpVerb = L"runas";
        sei.lpFile = L"powershell.exe";
        sei.lpParameters = command.c_str();
        sei.nShow = SW_HIDE;
        if (!ShellExecuteExW(&sei)) {
            DWORD errorCode = GetLastError();
            Log(LogLevel::Error, L"Failed to execute command as admin. Error code: " + std::to_wstring(errorCode));
            return false;
        }
        Log(LogLevel::Info, L"Executed admin command: " + command);
        return true;
    }
};

// -------------------------
// WebUI Checker (using WinHTTP)
// -------------------------
bool WaitForWebUI(const std::wstring& host, INTERNET_PORT port,
    std::chrono::milliseconds timeout = 15000ms,
    std::chrono::milliseconds interval = 1000ms)
{
    const auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < timeout)
    {
        // Open a WinHTTP session.
        HINTERNET hSession = WinHttpOpen(L"WebUI Checker/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (hSession)
        {
            // Connect to the host (localhost) on the given port.
            HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), port, 0);
            if (hConnect)
            {
                // Open an HTTP request.
                HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", L"/", nullptr,
                    WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
                if (hRequest)
                {
                    // Send the request.
                    if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                        WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
                    {
                        if (WinHttpReceiveResponse(hRequest, nullptr))
                        {
                            DWORD statusCode = 0;
                            DWORD size = sizeof(statusCode);
                            // Query the HTTP status code.
                            if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &size, WINHTTP_NO_HEADER_INDEX))
                            {
                                if (statusCode == 200)
                                {
                                    Log(LogLevel::Info, L"WebUI is up with status code 200.");
                                    WinHttpCloseHandle(hRequest);
                                    WinHttpCloseHandle(hConnect);
                                    WinHttpCloseHandle(hSession);
                                    return true;
                                }
                            }
                        }
                    }
                    WinHttpCloseHandle(hRequest);
                }
                WinHttpCloseHandle(hConnect);
            }
            WinHttpCloseHandle(hSession);
        }
        std::this_thread::sleep_for(interval);
    }
    Log(LogLevel::Warning, L"Timed out waiting for WebUI to become available.");
    return false;
}

// -------------------------
// Config Manager
// -------------------------
class ConfigManager {
public:
    static Config Load() {
        fs::path configPath = GetExecutablePath() / "config.json";
        Log(LogLevel::Info, L"Attempting to load config from: " + configPath.wstring());

        if (!fs::exists(configPath)) {
            Log(LogLevel::Warning, L"Config file does not exist. Creating default config...");
            CreateDefaultConfig(configPath);
            return {};
        }

        try {
            Log(LogLevel::Info, L"Reading config file...");
            std::ifstream file(configPath);
            json j = json::parse(file);

            Config config;
            config.ollamaPath = UTF8ToWString(j.at("ollamaPath").get<std::string>());
            config.dockerPath = UTF8ToWString(j.at("dockerPath").get<std::string>());

            Log(LogLevel::Info, L"Checking paths...");
            ValidatePaths(config);
            Log(LogLevel::Info, L"Configuration loaded successfully!");
            return config;
        }
        catch (const std::exception& e) {
            Log(LogLevel::Error, L"Error loading config: " + UTF8ToWString(e.what()));
            return {};
        }
    }

private:
    static fs::path GetExecutablePath() {
        wchar_t buffer[MAX_PATH];
        GetModuleFileNameW(nullptr, buffer, MAX_PATH);
        return fs::path(buffer).parent_path();
    }

    static void CreateDefaultConfig(const fs::path& path) {
        const json defaultConfig = {
            {"ollamaPath", ""},
            {"dockerPath", ""}
        };

        std::ofstream ofs(path);
        if (!ofs)
            throw std::runtime_error("Unable to create config file: " + path.string());
        ofs << defaultConfig.dump(4);
        ofs.close();

        Log(LogLevel::Info, L"Created default config.json in: " + path.wstring());
        Log(LogLevel::Info, L"Please edit config.json and set the following paths:");
        Log(LogLevel::Info, L"- ollamaPath (path to Ollama executable)");
        Log(LogLevel::Info, L"- dockerPath (path to Docker Desktop)");
    }

    static void ValidatePaths(const Config& config) {
        std::vector<std::pair<std::wstring, std::wstring>> paths = {
            {L"Ollama", config.ollamaPath},
            {L"Docker", config.dockerPath}
        };

        for (const auto& [name, path] : paths) {
            if (!fs::exists(path)) {
                throw std::runtime_error("Path does not exist: " +
                    std::string(path.begin(), path.end()));
            }
        }
    }
};

// -------------------------
// Main Application
// -------------------------
int main() {
    // Load configuration.
    const Config config = ConfigManager::Load();
    if (!config.isValid()) {
        Log(LogLevel::Error, L"Invalid configuration. Please check config.json");
        std::cin.get();
        return 1;
    }

    // Start Ollama and wait for one of its processes.
    Log(LogLevel::Info, L"Starting Ollama...");
    if (!ProcessManager::Start(config.ollamaPath)) {
        Log(LogLevel::Error, L"Failed to start Ollama.");
        return 1;
    }
    const std::vector<std::wstring> ollamaProcesses = {
        L"ollama app.exe",
        L"ollama.exe",
        L"ollama_llama_server.exe"
    };
    ProcessManager::WaitForAnyProcess(ollamaProcesses, 500ms, 10000ms);

    // Start Docker and wait for its process.
    Log(LogLevel::Info, L"Starting Docker...");
    if (!ProcessManager::Start(config.dockerPath)) {
        Log(LogLevel::Error, L"Failed to start Docker.");
        return 1;
    }
    ProcessManager::WaitForProcess(L"Docker Desktop.exe", 500ms, 10000ms);

    // Start Open WebUI container as admin.
    Log(LogLevel::Info, L"Starting Open WebUI container...");
    const std::wstring dockerCommand =
        L"docker run -d -p 3000:8080 --add-host=host.docker.internal:host-gateway "
        L"-v open-webui:/app/backend/data --name open-webui --restart always "
        L"ghcr.io/open-webui/open-webui:main";
    ProcessManager::ExecuteAsAdmin(dockerCommand);

    // Wait until WebUI is available before opening the browser.
    if (WaitForWebUI(L"localhost", 3000, 30000ms, 1000ms)) {
        Log(LogLevel::Info, L"Opening browser...");
        ShellExecuteW(nullptr, L"open", L"http://localhost:3000/", nullptr, nullptr, SW_SHOWNORMAL);
    }
    else {
        Log(LogLevel::Warning, L"WebUI did not become available within the timeout period.");
    }

    // Monitor Docker process.
    Log(LogLevel::Info, L"Monitoring Docker process...");
    ConsoleManager::Hide();
    while (ProcessManager::IsRunning(L"Docker Desktop.exe")) {
        std::this_thread::sleep_for(5000ms);
    }
    ConsoleManager::Show();

    Log(LogLevel::Info, L"Docker closed, shutting down...");

    // Kill all Ollama-related processes.
    const std::vector<std::wstring> processesToKill = {
        L"ollama app.exe",
        L"ollama.exe",
        L"ollama_llama_server.exe"
    };
    for (const auto& process : processesToKill) {
        ProcessManager::Kill(process);
    }

    // Shut down WSL.
    Log(LogLevel::Info, L"Shutting down WSL...");
    ProcessManager::ExecuteAsAdmin(L"wsl --shutdown");

    Log(LogLevel::Info, L"Shutdown process completed.");
    std::this_thread::sleep_for(2000ms);

    return 0;
}
