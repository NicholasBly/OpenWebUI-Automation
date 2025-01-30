#include <windows.h>
#include <tlhelp32.h>
#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#include <fstream>
#include <filesystem>
#include <vector>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
namespace fs = std::filesystem;
using namespace std::chrono_literals;

class ConsoleManager {
public:
    static void Hide() { ::ShowWindow(::GetConsoleWindow(), SW_HIDE); }
    static void Show() { ::ShowWindow(::GetConsoleWindow(), SW_SHOW); }
};

struct Config {
    std::wstring ollamaPath;
    std::wstring dockerPath;

    bool isValid() const {
        return !ollamaPath.empty() &&
            !dockerPath.empty();
    }
};

class ProcessManager {
public:
    static bool IsRunning(const std::wstring& processName) {
        const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE) return false;

        PROCESSENTRY32W pe32{ sizeof(pe32) };
        bool found = false;

        if (Process32FirstW(snapshot, &pe32)) {
            do {
                if (_wcsicmp(pe32.szExeFile, processName.c_str()) == 0) {
                    found = true;
                    break;
                }
            } while (Process32NextW(snapshot, &pe32));
        }

        CloseHandle(snapshot);
        return found;
    }

    static bool Start(const std::wstring& path) {
        STARTUPINFOW si{ sizeof(si) };
        PROCESS_INFORMATION pi{};

        const auto success = CreateProcessW(path.c_str(), nullptr, nullptr, nullptr,
            FALSE, 0, nullptr, nullptr, &si, &pi);

        if (success) {
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
        return success;
    }

    static void Kill(const std::wstring& processName) {
        const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE) return;

        PROCESSENTRY32W pe32{ sizeof(pe32) };
        if (Process32FirstW(snapshot, &pe32)) {
            do {
                if (_wcsicmp(pe32.szExeFile, processName.c_str()) == 0) {
                    if (const auto process = OpenProcess(PROCESS_TERMINATE, FALSE, pe32.th32ProcessID)) {
                        TerminateProcess(process, 0);
                        CloseHandle(process);
                    }
                }
            } while (Process32NextW(snapshot, &pe32));
        }
        CloseHandle(snapshot);
    }

    static bool ExecuteAsAdmin(const std::wstring& command) {
        SHELLEXECUTEINFOW sei{ sizeof(sei) };
        sei.lpVerb = L"runas";
        sei.lpFile = L"powershell.exe";
        sei.lpParameters = command.c_str();
        sei.nShow = SW_HIDE;
        return ShellExecuteExW(&sei);
    }
};

class ConfigManager {
public:
    static Config Load() {
        const auto configPath = GetExecutablePath() / "config.json";
        std::wcout << L"Attempting to load config from: " << configPath.wstring() << std::endl;

        if (!fs::exists(configPath)) {
            std::wcout << L"Config file does not exist. Creating default config..." << std::endl;
            CreateDefaultConfig(configPath);
            return {};
        }

        try {
            std::wcout << L"Reading config file..." << std::endl;
            std::ifstream file(configPath);
            json j = json::parse(file);

            Config config;
            config.ollamaPath = ToWString(j["ollamaPath"].get<std::string>());
            config.dockerPath = ToWString(j["dockerPath"].get<std::string>());

            std::wcout << L"Checking paths..." << std::endl;
            ValidatePaths(config);
            std::wcout << L"Configuration loaded successfully!" << std::endl;
            return config;
        }
        catch (const std::exception& e) {
            std::wcerr << L"Error loading config: " << e.what() << std::endl;
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

        std::ofstream(path) << defaultConfig.dump(4);

        std::wcout << L"Created default config.json in:\n";
        std::wcout << path.wstring() << L"\n";
        std::wcout << L"Please edit config.json and set the following paths:\n";
        std::wcout << L"- ollamaPath (path to Ollama executable)\n";
        std::wcout << L"- dockerPath (path to Docker Desktop)\n";
    }

    static std::wstring ToWString(const std::string& str) {
        return std::wstring(str.begin(), str.end());
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

int main() {
    const auto config = ConfigManager::Load();
    if (!config.isValid()) {
        std::wcout << L"Invalid configuration. Please check config.json\n";
        std::cin.get();
        return 1;
    }

    std::wcout << L"Starting Ollama..." << std::endl;
    ProcessManager::Start(config.ollamaPath);
    std::this_thread::sleep_for(5s);

    std::wcout << L"Starting Docker..." << std::endl;
    ProcessManager::Start(config.dockerPath);
    std::this_thread::sleep_for(5s);

    std::wcout << L"Starting Open WebUI container..." << std::endl;
    ProcessManager::ExecuteAsAdmin(L"docker run -d -p 3000:8080 --add-host=host.docker.internal:host-gateway "
        L"-v open-webui:/app/backend/data --name open-webui --restart always "
        L"ghcr.io/open-webui/open-webui:main");

    std::this_thread::sleep_for(10s);

    std::wcout << L"Opening browser..." << std::endl;
    ShellExecuteW(nullptr, L"open", L"http://localhost:3000/", nullptr, nullptr, SW_SHOWNORMAL);

    std::wcout << L"Monitoring Docker process..." << std::endl;
    ConsoleManager::Hide();
    while (ProcessManager::IsRunning(L"Docker Desktop.exe")) {
        std::this_thread::sleep_for(5s);
    }

    ConsoleManager::Show();
    std::wcout << L"Docker closed, shutting down...\n";

    const std::vector<std::wstring> processesToKill = {
        L"ollama app.exe",
        L"ollama.exe",
        L"ollama_llama_server.exe"
    };

    for (const auto& process : processesToKill) {
        ProcessManager::Kill(process);
    }

    std::wcout << L"Shutting down wsl..." << std::endl;
    ProcessManager::ExecuteAsAdmin(L"wsl --shutdown");

    std::wcout << L"Shutdown process completed." << std::endl;
    std::this_thread::sleep_for(2s);

    return 0;
}