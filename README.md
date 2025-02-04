# Docker WebUI Automation Tool

A lightweight C++ automation tool designed to streamline the process of launching and managing Docker, Ollama, and Open WebUI. The tool will monitor the Docker process and clean up after it has been closed.

## Features

- Config.json creation to define paths to executables
- Automated startup sequence for Docker Desktop and Ollama
- Automatic deployment of Open WebUI container and launches into your web browser when it's ready
- Monitors Docker process and starts the shutdown and cleanup processes when it has been closed (Right click Docker icon in tray and exit)
- Shutdown of all components when Docker closes
- WSL shutdown automation

## Prerequisites

- Windows 10/11
- Visual Studio 2022 (for editing source files)
- Docker Desktop
- Ollama
- WSL (Windows Subsystem for Linux)
- Administrator privileges for execution

## Installation

1. Clone the repository:
```bash
git clone https://github.com/NicholasBly/OpenWebUI-Automation.git
```

2. Open the solution in Visual Studio 2022

3. Configure the project properties:
   - Set C++ Language Standard to ISO C++17
   - Set Character Set to "Use Unicode Character Set"
   - Add required libraries: shell32.lib and user32.lib

4. Adjust paths in the config.json to match your system:
   - Ollama executable path
   - Docker Desktop executable path

## Usage

1. Build the solution in Visual Studio 2022

2. Run the executable as Administrator

The program will:
- Start Ollama
- Launch Docker Desktop
- Wait for Docker to be fully operational
- Deploy the Open WebUI container
- Open the WebUI in your default browser
- Monitor Docker status
- Perform cleanup when Docker is closed

## Configuration

### Docker Container Settings
The default Docker container configuration is:
```bash
docker run -d -p 3000:8080 --add-host=host.docker.internal:host-gateway -v open-webui:/app/backend/data --name open-webui --restart always ghcr.io/open-webui/open-webui:main
```

Modify these settings in the code as needed.

## Process Management

The tool actively monitors:
- Docker Desktop process
- Ollama process
- WSL status

When Docker Desktop is closed, the tool automatically:
1. Terminates Ollama
2. Shuts down WSL

## Building from Source

1. Open the solution in Visual Studio 2022
2. Set build configuration to Release/x64
3. Build the solution
4. Find the executable in the Release folder

## Contributing

1. Fork the repository
2. Create your feature branch (`git checkout -b feature/AmazingFeature`)
3. Commit your changes (`git commit -m 'Add some AmazingFeature'`)
4. Push to the branch (`git push origin feature/AmazingFeature`)
5. Open a Pull Request

## License

This project is licensed under the MIT License - see the LICENSE file for details.

## Acknowledgments

- Docker Desktop
- Ollama
- Open WebUI
