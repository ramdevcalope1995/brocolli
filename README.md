# 🥦 Brocolli: The Agentic Sandbox Computer

**Brocolli** is a high-performance, C-based sandbox API designed for building autonomous AI agents. It combines the power of Linux process isolation with integrated browser control and agentic reasoning, providing a "built-in batteries included" foundation for the next generation of AI systems.

> **Note**: Brocolli serves as the core sandbox framework powering [www.brocolli.xyz](https://www.brocolli.xyz), a hybrid sandbox platform that leverages this implementation as its foundation for secure, isolated execution environments.

## 🚀 Features

- **🛡️ Native Sandbox**: Isolated execution environments using Linux Namespaces (PID, Mount, UTS, IPC, Network) and Seccomp syscall filtering.
- **🌐 Integrated Browser**: Headless Chromium control via Chrome DevTools Protocol (CDP), inspired by **Skyvern**.
- **🤖 Agentic AI**: High-performance agentic reasoning powered by **QuantClaw** (a C++ implementation of OpenClaw).
- **⏳ Durable Workflows**: SQLite-backed background job queue with exponential backoff, inspired by **Inngest**.
- **⚡ Built in C**: Designed for extreme performance, low memory footprint, and minimal overhead using the **Kore** web framework.

## 🏗️ Architecture

Brocolli is built as a modular system with four core engines:

| Engine | Responsibility | Inspiration |
| :--- | :--- | :--- |
| **Sandbox** | Process isolation & security | Manus "OK Computer" like |
| **Browser** | Web automation & navigation | Skyvern like |
| **Workflow** | Durable background tasks | Inngest like |
| **Agent** | High-level reasoning & tool use | QuantClaw like |

## 🛠️ Getting Started

### Prerequisites

- Linux (Ubuntu 22.04+ recommended)
- `libseccomp-dev`, `sqlite3`, `libcurl`, `openssl`
- `chromium-browser` (for the browser engine)
- `kore` (web framework)

### Installation

```bash
git clone hhttps://github.com/ramdevcalope1995/brocolli.git
cd brocolli
make
```

### Running the API

```bash
kore -c conf/brocolli.conf
```

The API will be available at `http://localhost:8888`.

## Deploying the API for usage

Refer to the DEPLOYMENT.md documentation.

## 📖 API Reference

### Sandbox API
- `POST /api/sandbox`: Create a new sandbox.
- `POST /api/sandbox/:id/exec`: Execute a command inside a sandbox.
- `DELETE /api/sandbox/:id`: Destroy a sandbox.

### Browser API
- `POST /api/browser/launch`: Start a headless browser.
- `POST /api/browser/:id/navigate`: Navigate to a URL.

### Agent API
- `POST /api/agent/task`: Create a high-level agentic task.
- `GET /api/agent/:id/status`: Get the status of an ongoing task.

## 🤝 Contributing

Brocolli is an open-source project. We welcome contributions in the form of bug reports, feature requests, and pull requests. Please see `CONTRIBUTING.md` for more details.

## 📄 License

This project is licensed under the MIT License - see the `LICENSE` file for details.

## 👤 Author

**Ramdev G. Calope** - [LinkedIn](https://www.linkedin.com/in/ramdevcalope)
