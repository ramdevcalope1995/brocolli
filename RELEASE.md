# Brocolli v0.0.1-beta Release Notes

**Release Date:** March 29, 2026  
**Status:** Beta  
**Repository:** https://github.com/your-username/brocolli

---

## 🎉 Welcome to Brocolli Beta

Brocolli is a high-performance, C-based sandbox API designed for building autonomous AI agents. This beta release introduces the complete foundation for the "agentic sandbox computer" vision, combining Linux process isolation, integrated browser control, durable workflows, and agentic reasoning powered by QuantClaw.

---

## ✨ What's New in v0.0.1-beta

### Core Engines

#### 🛡️ Sandbox Engine
- **Linux Namespace Isolation:** Full support for PID, Mount, UTS, IPC, and Network namespaces
- **Seccomp Filtering:** Whitelist-based syscall filtering for enhanced security
- **Persistent State:** SQLite-backed sandbox metadata ensures survival across API restarts
- **Resource Limits:** Memory and CPU constraints via `setrlimit` and cgroups preparation
- **Namespace Holder Pattern:** Efficient namespace lifecycle management using the "holder process" pattern

**Key Features:**
- Create isolated sandboxes with custom hostnames
- Execute commands inside sandboxes with full I/O capture
- Destroy sandboxes and automatically clean up all resources
- Query active sandbox state

#### 🌐 Browser Engine
- **Headless Chromium Integration:** Launch and manage headless Chromium instances
- **Chrome DevTools Protocol (CDP) Foundation:** WebSocket-based communication structure ready for full CDP implementation
- **Process Management:** Automatic browser lifecycle management with configurable remote debugging ports
- **Screenshot Support:** Framework for capturing page screenshots (implementation pending)

**Key Features:**
- Launch headless Chromium with custom debugging ports
- Navigate to URLs
- Capture page state
- Graceful browser shutdown

#### ⏳ Workflow Engine
- **SQLite Job Queue:** Durable, persistent job storage with WAL mode for concurrency
- **Exponential Backoff:** Automatic retry logic with configurable backoff strategies
- **Job Status Tracking:** Complete job lifecycle management (pending, running, completed, failed)
- **Background Worker:** Single-threaded worker with future multi-process scaling capability

**Key Features:**
- Enqueue background jobs with custom payloads
- Automatic retry with exponential backoff
- Job status queries and history
- Persistent job state across restarts

#### 🤖 Agent Engine (QuantClaw Bridge)
- **QuantClaw Gateway Integration:** WebSocket RPC client for communicating with QuantClaw (port 18800)
- **Agentic Task Management:** High-level goal-based task creation and monitoring
- **Tool Registration:** Framework for registering Sandbox and Browser as skills in QuantClaw
- **Multi-Provider LLM Support:** Ready for OpenAI, Anthropic, and local model integration

**Key Features:**
- Create agentic tasks with natural language goals
- Monitor task progress and status
- Interrupt and cancel ongoing tasks
- Skill-based tool integration

### API Endpoints

#### Sandbox API
```
POST   /api/sandbox                    - Create a new sandbox
POST   /api/sandbox/:id/exec           - Execute a command in a sandbox
DELETE /api/sandbox/:id                - Destroy a sandbox
GET    /api/sandbox/:id                - Get sandbox status
```

#### Browser API
```
POST   /api/browser/launch             - Start a headless browser
POST   /api/browser/:id/navigate       - Navigate to a URL
POST   /api/browser/:id/screenshot     - Capture a screenshot
DELETE /api/browser/:id                - Close the browser
```

#### Workflow API
```
POST   /api/jobs                       - Enqueue a background job
GET    /api/jobs/:id                   - Get job status
DELETE /api/jobs/:id                   - Cancel a job
```

#### Agent API
```
POST   /api/agent/task                 - Create an agentic task
GET    /api/agent/:id/status           - Get task status
DELETE /api/agent/:id                  - Cancel an agentic task
```

### JSON Request/Response Handling
- **cJSON Integration:** Proper JSON parsing for all API requests
- **Structured Responses:** Consistent JSON response format across all endpoints
- **Error Handling:** Clear error messages with HTTP status codes

### Containerization
- **Multi-Stage Dockerfile:** Optimized build process with separate build and runtime stages
- **Docker Compose Orchestration:** Complete stack with Brocolli API and QuantClaw gateway
- **Health Checks:** Automatic container restart on failure
- **Linux Capabilities:** Proper `CAP_SYS_ADMIN`, `CAP_SYS_PTRACE`, `CAP_NET_ADMIN` for sandbox creation

### Open Source Readiness
- **Comprehensive README.md:** Project overview, features, and quick start guide
- **DEPLOYMENT.md:** Detailed deployment instructions for Docker and Docker Compose
- **CONTRIBUTING.md:** Guidelines for open-source contributors
- **MIT License:** Permissive open-source license
- **.gitignore:** Proper exclusion of build artifacts, logs, and data files

---

## 🐛 Known Issues & Limitations

### Sandbox Engine
- **User Namespaces:** `CLONE_NEWUSER` support not yet implemented (requires root or specific capabilities)
- **Rootfs Support:** No custom root filesystem support yet; uses host filesystem
- **Cgroups v2:** Preparation in place but full cgroups v2 integration pending
- **Network Namespace:** Network namespace creation works but network configuration is minimal

### Browser Engine
- **CDP Implementation:** WebSocket connection and command sending are stubs; full CDP protocol implementation pending
- **Screenshot Capture:** Framework in place but actual screenshot data capture not yet implemented
- **Navigation Timeout:** No timeout handling for slow page loads
- **Multi-Tab Support:** Single-tab only; multi-tab automation pending

### Agent Engine
- **WebSocket RPC:** Connection to QuantClaw gateway is a stub; full JSON RPC implementation pending
- **Skill Registration:** Framework ready but actual skill manifest integration with QuantClaw pending
- **Error Recovery:** Limited error handling for QuantClaw communication failures
- **Streaming Responses:** No support for streaming agent responses yet

### Workflow Engine
- **Single Worker:** Only one background worker thread; multi-process scaling pending
- **Job Dependencies:** No support for job dependencies or workflow steps
- **Scheduled Jobs:** No cron or scheduled job support yet

### General
- **TLS/HTTPS:** Kore framework supports TLS but not yet configured in default setup
- **Authentication:** No built-in authentication or authorization yet
- **Rate Limiting:** No rate limiting or quota management
- **Monitoring:** Basic logging only; no metrics or observability integration

---

## 📋 What's Coming in Future Releases

### v0.1.0 (Planned)
- Full Chrome DevTools Protocol (CDP) implementation for browser automation
- Complete QuantClaw WebSocket RPC bridge with skill registration
- User Namespace support for non-root sandbox creation
- Custom rootfs support with `pivot_root`
- Multi-process workflow worker pool
- Basic authentication and authorization

### v0.2.0 (Planned)
- Cgroups v2 full integration for resource management
- Network namespace configuration and bridge setup
- Job dependencies and workflow steps
- Scheduled jobs (cron-like) support
- Prometheus metrics and observability
- OpenTelemetry tracing

### v0.3.0 (Planned)
- Kubernetes operator for Brocolli deployment
- Multi-tenant sandbox isolation
- Advanced security policies (AppArmor, SELinux)
- Sandbox resource usage analytics
- Web UI dashboard for sandbox management

---

## 🚀 Getting Started

### Quick Start with Docker Compose

```bash
git clone https://github.com/your-username/brocolli.git
cd brocolli
docker-compose up --build
```

The API will be available at `http://localhost:8888`.

### Create Your First Sandbox

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

---

## 📚 Documentation

- **[README.md](./README.md):** Project overview and features
- **[DEPLOYMENT.md](./DEPLOYMENT.md):** Deployment guide and configuration
- **[CONTRIBUTING.md](./CONTRIBUTING.md):** Contribution guidelines
- **[Architecture Analysis](./brocolli_architecture_analysis.md):** Detailed technical analysis

---

## 🙏 Contributing

Brocolli is an open-source project and welcomes contributions from the community. Whether you're fixing bugs, adding features, or improving documentation, your help is appreciated!

See [CONTRIBUTING.md](./CONTRIBUTING.md) for guidelines on how to contribute.

---

## 🔗 Links

- **GitHub:** https://github.com/your-username/brocolli
- **Website:** https://brocolli.xyz
- **Issues:** https://github.com/your-username/brocolli/issues
- **Discussions:** https://github.com/your-username/brocolli/discussions

---

## 📝 Changelog

### v0.0.1-beta (March 29, 2026)
- Initial beta release
- Complete Sandbox Engine with Linux Namespaces and Seccomp
- Browser Engine foundation with Chromium integration
- Workflow Engine with SQLite job queue
- Agent Engine with QuantClaw bridge
- Docker and Docker Compose support
- Comprehensive documentation and deployment guides

---

## ⚠️ Beta Release Disclaimer

This is a **beta release** and is not recommended for production use without thorough testing. While the core functionality is stable, there may be breaking changes in future releases as we refine the API and add new features.

Please report any bugs or issues on [GitHub Issues](https://github.com/your-username/brocolli/issues).

---

## 📄 License

Brocolli is licensed under the MIT License. See [LICENSE](./LICENSE) for details.

---

**Thank you for trying Brocolli! We're excited to build the future of agentic sandbox computing with you.** 🥦
