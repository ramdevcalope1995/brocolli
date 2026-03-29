<div align="center">
  <img src="https://i.postimg.cc/fT86cQHs/imresizer-logo.png" alt="Brocolli Logo" width="280" height="280" />
  
  # Brocolli 
  
  ### The Agentic Sandbox Computer
  
  A high-performance, C-based sandbox API designed for building autonomous AI agents.
  
  [![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
  [![GitHub Stars](https://img.shields.io/github/stars/ramdevcalope1995/brocolli?style=social)](https://github.com/ramdevcalope1995/brocolli)
  [![GitHub Issues](https://img.shields.io/github/issues/ramdevcalope1995/brocolli)](https://github.com/ramdevcalope1995/brocolli/issues)
  
</div>

---

**Brocolli** combines the power of Linux process isolation with integrated browser control and agentic reasoning, providing a "built-in batteries included" foundation for the next generation of AI systems.

> **Note**: This is the core engine behind [brocolli.xyz](https://brocolli.xyz), a hybrid sandbox platform for secure, isolated execution environments with agentic AI capabilities.

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
git clone https://github.com/ramdevcalope1995/brocolli
cd brocolli
make
```

### Running the API

```bash
kore -c conf/brocolli.conf
```

The API will be available at `http://localhost:8888`.

### Docker Deployment

For a complete, containerized setup:

```bash
docker-compose up --build
```

This will start:
- **Brocolli API** on port 8888
- **QuantClaw Gateway** on port 18800
- **SQLite Database** for persistent state

See [DEPLOYMENT.md](./DEPLOYMENT.md) for detailed configuration options.

## 📚 API Examples

### Create a Sandbox

```bash
curl -X POST http://localhost:8888/api/sandbox \
  -H "Content-Type: application/json" \
  -d '{
    "net_enabled": false,
    "mem_limit_bytes": 536870912,
    "hostname": "my-sandbox"
  }'
```

### Execute a Command

```bash
curl -X POST http://localhost:8888/api/sandbox/{sandbox_id}/exec \
  -H "Content-Type: application/json" \
  -d '{
    "path": "/bin/bash",
    "args": ["-c", "echo Hello from Brocolli!"]
  }'
```

### Launch a Browser

```bash
curl -X POST http://localhost:8888/api/browser/launch \
  -H "Content-Type: application/json" \
  -d '{
    "headless": true,
    "debug_port": 9222
  }'
```

### Create an Agentic Task

```bash
curl -X POST http://localhost:8888/api/agent/task \
  -H "Content-Type: application/json" \
  -d '{
    "goal": "Research the latest AI trends and summarize findings",
    "model": "openai/gpt-4",
    "timeout_seconds": 300
  }'
```

## 📖 Documentation

- **[README.md](./README.md)** - Project overview and features
- **[DEPLOYMENT.md](./DEPLOYMENT.md)** - Deployment guide and configuration
- **[RELEASE_NOTES.md](./RELEASE_NOTES.md)** - Version history and changelog
- **[CONTRIBUTING.md](./CONTRIBUTING.md)** - Contribution guidelines
- **[Architecture Analysis](./brocolli_architecture_analysis.md)** - Detailed technical analysis

## 🔧 Components

### Sandbox Engine (`src/sandbox.c`)
Provides isolated execution environments using Linux Namespaces and Seccomp filtering. Supports:
- Process isolation (PID namespace)
- Filesystem isolation (Mount namespace)
- Hostname isolation (UTS namespace)
- IPC isolation (IPC namespace)
- Network isolation (Network namespace)
- Syscall filtering (Seccomp)
- Resource limits (memory, CPU)

### Browser Engine (`src/browser.c`)
Manages headless Chromium instances and provides web automation capabilities:
- Launch headless browsers
- Navigate to URLs
- Capture screenshots
- Execute JavaScript
- Handle navigation events

### Workflow Engine (`src/workflow.c`)
Durable background job queue with SQLite persistence:
- Enqueue jobs with custom payloads
- Automatic retry with exponential backoff
- Job status tracking
- Concurrent job execution
- Persistent state across restarts

### Agent Engine (`src/agent.c`)
High-level agentic reasoning powered by QuantClaw:
- Create agentic tasks with natural language goals
- Multi-provider LLM support (OpenAI, Anthropic, local models)
- Tool registration and skill management
- Task monitoring and interruption
- Context management

## 🔐 Security

Brocolli prioritizes security through:
- **Kernel-level isolation** using Linux Namespaces
- **Syscall filtering** via Seccomp to prevent dangerous operations
- **Resource limits** to prevent resource exhaustion attacks
- **Capability dropping** to minimize privileges
- **No network access** by default (can be enabled per sandbox)

## 🚀 Performance

Built in pure C with the Kore web framework for:
- **Minimal overhead**: Low memory footprint per sandbox
- **High throughput**: Handle thousands of concurrent sandboxes
- **Low latency**: Sub-millisecond API response times
- **Efficient resource usage**: Optimized for cloud deployments

## 🤝 Contributing

We welcome contributions from the community! Whether you're fixing bugs, adding features, or improving documentation, your help is appreciated.

See [CONTRIBUTING.md](./CONTRIBUTING.md) for guidelines on how to contribute.

## 📋 Roadmap

### v0.1.0 (Planned)
- Full Chrome DevTools Protocol (CDP) implementation
- Complete QuantClaw WebSocket RPC bridge
- User Namespace support for non-root execution
- Custom rootfs support with `pivot_root`
- Multi-process workflow workers

### v0.2.0 (Planned)
- Cgroups v2 full integration
- Network namespace configuration
- Job dependencies and workflow steps
- Scheduled jobs (cron-like support)
- Prometheus metrics and observability

### v0.3.0 (Planned)
- Kubernetes operator
- Multi-tenant isolation
- Advanced security policies
- Web UI dashboard
- Resource usage analytics

## 📄 License

Brocolli is licensed under the **MIT License**. See [LICENSE](./LICENSE) for details.

## 🔗 Links

- **Website**: [brocolli.xyz](https://brocolli.xyz)
- **GitHub**: [github.com/ramdevcalope1995/brocolli](https://github.com/ramdevcalope1995/brocolli)
- **Issues**: [github.com/ramdevcalope1995/brocolli/issues](https://github.com/ramdevcalope1995/brocolli/issues)
- **Discussions**: [github.com/ramdevcalope1995/brocolli/discussions](https://github.com/ramdevcalope1995/brocolli/discussions)

## 🙏 Acknowledgments

Brocolli is inspired by and builds upon the excellent work of:
- **Skyvern** - Browser automation and web scraping
- **Inngest** - Durable workflow execution
- **QuantClaw** - Agentic AI reasoning
- **Kore** - High-performance web framework
- **OpenSandbox** - Sandbox architecture concepts

---

<div align="center">
  
  **Built with ❤️ for the open-source community**
  
  Made by developers, for developers.
  
</div>
