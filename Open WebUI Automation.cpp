#include <windows.h>
#include <tlhelp32.h>
#include <tchar.h>
#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
namespace fs = std::filesystem;

void HideConsole()
{
    ::ShowWindow(::GetConsoleWindow(), SW_HIDE);
}

void ShowConsole()
{
    ::ShowWindow(::GetConsoleWindow(), SW_SHOW);
}

struct Config {
    std::wstring ollamaPath;
    std::wstring dockerPath;
    bool isValid() const {
        return !ollamaPath.empty() && !dockerPath.empty();
    }
};

void createDefaultConfig(const std::string& configPath) {
    json defaultConfig = {
        {"ollamaPath", ""},
        {"dockerPath", ""}
    };

    std::ofstream configFile(configPath);
    configFile << defaultConfig.dump(4);
}

std::string GetExecutablePath() {
    wchar_t buffer[MAX_PATH];
    GetModuleFileNameW(NULL, buffer, MAX_PATH);
    std::wstring ws(buffer);
    std::string path(ws.begin(), ws.end());
    return path.substr(0, path.find_last_of("\\/"));
}

Config loadConfig() {
    Config config;
    std::string exePath = GetExecutablePath();
    const std::string configPath = exePath + "\\config.json";

    std::wcout << L"Attempting to load config from: " << std::wstring(configPath.begin(), configPath.end()) << std::endl;

    // Create default config if it doesn't exist
    if (!fs::exists(configPath)) {
        std::wcout << L"Config file does not exist. Creating default config..." << std::endl;
        createDefaultConfig(configPath);
        std::wcout << L"Created default config.json in:\n";
        std::wcout << std::wstring(configPath.begin(), configPath.end()) << L"\n";
        std::wcout << L"Please edit config.json and set the following paths:\n";
        std::wcout << L"- ollamaPath (path to Ollama executable)\n";
        std::wcout << L"- dockerPath (path to Docker Desktop)\n";
        return config;
    }

    try {
        std::wcout << L"Reading config file..." << std::endl;
        std::ifstream configFile(configPath);
        if (!configFile.is_open()) {
            std::wcout << L"Error: Could not open config file!" << std::endl;
            return config;
        }

        json j;
        configFile >> j;

        // Debug output
        //std::wcout << L"Config file contents:" << std::endl;
        //std::cout << j.dump(4) << std::endl;

        // Convert JSON strings to wstring
        std::string ollamaPath = j["ollamaPath"];
        std::string dockerPath = j["dockerPath"];

        std::wcout << L"Checking paths..." << std::endl;

        // Check each path individually and report status
        if (!fs::exists(ollamaPath)) {
            std::wcout << L"Error: Ollama path does not exist: " <<
                std::wstring(ollamaPath.begin(), ollamaPath.end()) << std::endl;
            return config;
        }
        if (!fs::exists(dockerPath)) {
            std::wcout << L"Error: Docker path does not exist: " <<
                std::wstring(dockerPath.begin(), dockerPath.end()) << std::endl;
            return config;
        }

        //std::wcout << L"All paths exist. Converting to wstring..." << std::endl;

        // Convert paths to wstring
        config.ollamaPath = std::wstring(ollamaPath.begin(), ollamaPath.end());
        config.dockerPath = std::wstring(dockerPath.begin(), dockerPath.end());

        std::wcout << L"Configuration loaded successfully!" << std::endl;

    }
    catch (const json::exception& e) {
        std::wcout << L"JSON parsing error: " << e.what() << std::endl;
        return config;
    }
    catch (const std::exception& e) {
        std::wcout << L"Error loading config: " << e.what() << std::endl;
        return config;
    }

    return config;
}

bool ExecuteCommandAsAdmin(const wchar_t* command) {
    SHELLEXECUTEINFO sei = { sizeof(sei) };
    sei.lpVerb = L"runas";  // Request elevation
    sei.lpFile = L"powershell.exe";
    sei.lpParameters = command;
    sei.nShow = SW_HIDE;
    return ShellExecuteEx(&sei);
}

bool IsProcessRunning(const wchar_t* processName) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return false;

    PROCESSENTRY32W pe32;
    pe32.dwSize = sizeof(pe32);

    if (!Process32FirstW(snapshot, &pe32)) {
        CloseHandle(snapshot);
        return false;
    }

    do {
        if (_wcsicmp(pe32.szExeFile, processName) == 0) {
            CloseHandle(snapshot);
            return true;
        }
    } while (Process32NextW(snapshot, &pe32));

    CloseHandle(snapshot);
    return false;
}

bool StartProcess(const wchar_t* path) {
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;

    if (CreateProcessW(path, NULL, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return true;
    }
    return false;
}

void KillProcess(const wchar_t* processName) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return;

    PROCESSENTRY32W pe32;
    pe32.dwSize = sizeof(pe32);

    if (Process32FirstW(snapshot, &pe32)) {
        do {
            if (_wcsicmp(pe32.szExeFile, processName) == 0) {
                HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pe32.th32ProcessID);
                if (hProcess != NULL) {
                    TerminateProcess(hProcess, 0);
                    CloseHandle(hProcess);
                }
            }
        } while (Process32NextW(snapshot, &pe32));
    }

    CloseHandle(snapshot);
}

// Function to open URL in default browser
void OpenBrowser(const wchar_t* url) {
    ShellExecuteW(NULL, L"open", url, NULL, NULL, SW_SHOWNORMAL);
}

int main() {
    // Load configuration
    Config config = loadConfig();
    if (!config.isValid()) {
        std::wcout << L"Invalid configuration. Please check config.json and try again.\n";
        std::wcout << L"Press any key to exit...";
        std::cin.get();
        return 1;
    }

    // Start Ollama
    std::wcout << L"Starting Ollama..." << std::endl;
    StartProcess(config.ollamaPath.c_str());
    std::this_thread::sleep_for(std::chrono::seconds(5));

    // Start Docker
    std::wcout << L"Starting Docker..." << std::endl;
    StartProcess(config.dockerPath.c_str());
    std::this_thread::sleep_for(std::chrono::seconds(5));

    // Run docker command
    std::wcout << L"Starting Open WebUI container..." << std::endl;
    ExecuteCommandAsAdmin(L"docker run -d -p 3000:8080 --add-host=host.docker.internal:host-gateway -v open-webui:/app/backend/data --name open-webui --restart always ghcr.io/open-webui/open-webui:main");

    // Wait for container to start
    std::this_thread::sleep_for(std::chrono::seconds(8));

    // Open browser
    std::wcout << L"Opening browser..." << std::endl;
    OpenBrowser(L"http://localhost:3000/");

    // Monitor Docker
    std::wcout << L"Monitoring Docker process..." << std::endl;
    HideConsole();
    while (true) {
        if (!IsProcessRunning(L"Docker Desktop.exe")) {
            ShowConsole();
            std::wcout << L"Docker closed, shutting down..." << std::endl;

            // Stop Ollama
            KillProcess(L"ollama app.exe");
            KillProcess(L"ollama.exe");
            KillProcess(L"ollama_llama_server.exe");

            // Shutdown WSL
            std::wcout << L"Shutting down wsl..." << std::endl;
            ExecuteCommandAsAdmin(L"wsl --shutdown");

            std::wcout << L"Shutdown process completed." << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(3));

            break;
        }
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }

    return 0;
}