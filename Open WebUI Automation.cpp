#include <windows.h>
#include <tlhelp32.h>
#include <tchar.h>
#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#include <fstream>
#include <sstream>

// Function to execute commands as administrator
bool ExecuteCommandAsAdmin(const wchar_t* command) {
    SHELLEXECUTEINFO sei = { sizeof(sei) };
    sei.lpVerb = L"runas";  // Request elevation
    sei.lpFile = L"powershell.exe";
    sei.lpParameters = command;
    sei.nShow = SW_HIDE;
    return ShellExecuteEx(&sei);
}

// Function to check if a process is running
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

// Function to start a process
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

// Function to kill a process by name
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

// Function to modify config.json
bool ModifyConfig(bool startMinimized) {
    const std::string configPath = "D:\\Users\\Gamer\\Desktop\\FPS Latency Tools\\Limit-nvpstate\\limit-nvpstate\\config.json";
    std::ifstream inFile(configPath);
    if (!inFile) return false;

    std::stringstream buffer;
    buffer << inFile.rdbuf();
    inFile.close();

    std::string content = buffer.str();
    const std::string trueStr = "\"start_minimized\": true";
    const std::string falseStr = "\"start_minimized\": false";

    size_t posTrue = content.find(trueStr);
    size_t posFalse = content.find(falseStr);

    if (posTrue != std::string::npos) {
        if (!startMinimized) {
            content.replace(posTrue, trueStr.length(), falseStr);
        }
    }
    else if (posFalse != std::string::npos) {
        if (startMinimized) {
            content.replace(posFalse, falseStr.length(), trueStr);
        }
    }
    else {
        std::wcout << L"Could not find start_minimized setting in config file!" << std::endl;
        return false;
    }

    std::ofstream outFile(configPath);
    if (!outFile) return false;

    outFile << content;
    outFile.close();

    return true;
}

// Function to open URL in default browser
void OpenBrowser(const wchar_t* url) {
    ShellExecuteW(NULL, L"open", url, NULL, NULL, SW_SHOWNORMAL);
}

int main() {
    // Kill limit-nvpstate if running
    std::wcout << L"Stopping limit-nvpstate..." << std::endl;
    KillProcess(L"limit-nvpstate.exe");

    // Modify config
    std::wcout << L"Modifying config..." << std::endl;
    if (!ModifyConfig(false)) {
        std::wcout << L"Failed to modify config file!" << std::endl;
        return 1;
    }

    // Start limit-nvpstate
    std::wcout << L"Starting limit-nvpstate..." << std::endl;
    StartProcess(L"D:\\Users\\Gamer\\Desktop\\FPS Latency Tools\\Limit-nvpstate\\limit-nvpstate\\limit-nvpstate.exe");

    // Start Ollama
    std::wcout << L"Starting Ollama..." << std::endl;
    StartProcess(L"C:\\Users\\Gamer\\AppData\\Local\\Programs\\Ollama\\ollama app.exe");
    std::this_thread::sleep_for(std::chrono::seconds(5));  // Give Ollama time to start

    // Start Docker
    std::wcout << L"Starting Docker..." << std::endl;
    StartProcess(L"C:\\Program Files\\Docker\\Docker\\Docker Desktop.exe");
    std::this_thread::sleep_for(std::chrono::seconds(5));  // Give Docker time to start

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
    while (true) {
        if (!IsProcessRunning(L"Docker Desktop.exe")) {
            std::wcout << L"Docker closed, shutting down..." << std::endl;

            // Stop Ollama
            KillProcess(L"ollama app.exe");
            KillProcess(L"ollama.exe");
            KillProcess(L"ollama_llama_server.exe");

            // Make limit-nvpstate run minimized again once we're done
            ModifyConfig(true);

            // Start limit-nvpstate
            std::wcout << L"Starting limit-nvpstate..." << std::endl;
            StartProcess(L"D:\\Users\\Gamer\\Desktop\\FPS Latency Tools\\Limit-nvpstate\\limit-nvpstate\\limit-nvpstate.exe");

            // Shutdown WSL
            std::wcout << L"Shutting down wsl..." << std::endl;
            ExecuteCommandAsAdmin(L"wsl --shutdown");

            break;
        }
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }

    return 0;
}