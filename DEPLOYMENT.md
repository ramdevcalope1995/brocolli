# Brocolli Deployment Guide

This guide covers how to deploy and run Brocolli using Docker and Docker Compose.

## Prerequisites

- Docker (20.10+)
- Docker Compose (2.0+)
- At least 4GB of available RAM
- Linux kernel with support for Linux Namespaces and Seccomp (Ubuntu 20.04+)

## Quick Start

### 1. Clone and Build

```bash
git clone https://github.com/ramdevcalope1995/brocolli.git
cd brocolli
```

### 2. Build the Docker Image

```bash
docker build -t brocolli:latest .
```

Or use Docker Compose to build and run everything:

```bash
docker-compose up --build
```

### 3. Access the Services

Once running, the following services are available:

- **Brocolli API:** `http://localhost:8888`
- **QuantClaw WebSocket Gateway:** `ws://localhost:18800`
- **QuantClaw HTTP API / Dashboard:** `http://localhost:18801`

## Docker Compose Services

### Brocolli API Server

The main API server that provides:
- Sandbox creation and execution
- Browser control
- Agentic task management
- Background job workflows

**Ports:** `8888` (HTTP)

**Volumes:**
- `./data`: Persistent data storage (databases, etc.)
- `./logs`: Application logs

**Environment Variables:**
- `KORE_ENV`: Set to `production` for production deployments
- `LOG_LEVEL`: Logging level (debug, info, warn, error)

### QuantClaw Gateway

The agentic reasoning engine that powers high-level task automation.

**Ports:**
- `18800`: WebSocket RPC Gateway
- `18801`: HTTP REST API / Dashboard

**Volumes:**
- `./quantclaw-config`: QuantClaw configuration files
- `./quantclaw-workspace`: Workspace, skills, and session data

## Configuration

### Brocolli Configuration

Edit `conf/brocolli.conf` to customize:
- API port (default: 8888)
- Worker processes
- TLS settings
- Module paths

### QuantClaw Configuration

Create or edit `quantclaw-config/quantclaw.json`:

```json
{
  "system": {
    "logLevel": "info"
  },
  "llm": {
    "model": "openai/gpt-5.4",
    "maxIterations": 15,
    "temperature": 0.7,
    "maxTokens": 4096
  },
  "providers": {
    "openai": {
      "apiKey": "YOUR_OPENAI_API_KEY",
      "baseUrl": "https://api.openai.com/v1",
      "timeout": 30
    }
  },
  "gateway": {
    "port": 18800,
    "bind": "0.0.0.0",
    "auth": {
      "mode": "token",
      "token": "YOUR_SECRET_TOKEN"
    }
  }
}
```

## Running Brocolli

### Using Docker Compose (Recommended)

```bash
# Start all services
docker-compose up

# Start in background
docker-compose up -d

# View logs
docker-compose logs -f brocolli

# Stop all services
docker-compose down
```

### Using Docker Directly

```bash
# Build the image
docker build -t brocolli:latest .

# Run the container
docker run -d \
  --name brocolli-api \
  -p 8888:8888 \
  --cap-add=SYS_ADMIN \
  --cap-add=SYS_PTRACE \
  --cap-add=NET_ADMIN \
  --security-opt apparmor=unconfined \
  --security-opt seccomp=unconfined \
  -v $(pwd)/data:/app/data \
  -v $(pwd)/logs:/app/logs \
  brocolli:latest
```

## API Examples

### Create a Sandbox

```bash
curl -X POST http://localhost:8888/api/sandbox \
  -H "Content-Type: application/json" \
  -d '{
    "net_enabled": false,
    "mem_limit_bytes": 536870912,
    "hostname": "brocolli-box"
  }'
```

### Execute a Command in a Sandbox

```bash
curl -X POST http://localhost:8888/api/sandbox/{sandbox_id}/exec \
  -H "Content-Type: application/json" \
  -d '{
    "path": "/bin/bash",
    "args": ["-c", "echo Hello from Brocolli"]
  }'
```

### Create an Agentic Task

```bash
curl -X POST http://localhost:8888/api/agent/task \
  -H "Content-Type: application/json" \
  -d '{
    "goal": "Research the latest AI trends and summarize them in a markdown file."
  }'
```

## Security Considerations

### Linux Capabilities

The Docker container requires the following capabilities to manage Linux Namespaces:
- `SYS_ADMIN`: Required for `clone()` with namespace flags
- `SYS_PTRACE`: Required for `ptrace()` operations
- `NET_ADMIN`: Required for network namespace operations

### Seccomp Profile

The container runs with an unconfined seccomp profile to allow the full range of syscalls needed for sandbox creation. For production deployments, consider creating a custom seccomp profile that whitelists only necessary syscalls.

### AppArmor

The container runs with an unconfined AppArmor profile. For production, create a custom AppArmor profile.

## Troubleshooting

### Container fails to start

Check the logs:
```bash
docker-compose logs brocolli
```

Ensure your Linux kernel supports Namespaces and Seccomp:
```bash
grep -E "CONFIG_NAMESPACES|CONFIG_SECCOMP" /boot/config-$(uname -r)
```

### Chromium not found

The container installs Chromium automatically. If it fails:
```bash
docker-compose exec brocolli apt-get update && apt-get install -y chromium-browser
```

### QuantClaw connection errors

Ensure the QuantClaw service is running:
```bash
docker-compose ps
docker-compose logs quantclaw
```

Check network connectivity between containers:
```bash
docker-compose exec brocolli ping quantclaw
```

## Production Deployment

For production deployments, consider:

1. **Use a reverse proxy:** Deploy Nginx or HAProxy in front of Brocolli
2. **Enable TLS:** Configure Kore with SSL certificates
3. **Resource limits:** Set CPU and memory limits in docker-compose.yml
4. **Monitoring:** Integrate with Prometheus, Grafana, or similar
5. **Logging:** Use centralized logging (ELK, Loki, etc.)
6. **Backups:** Regularly backup the `data/` volume
7. **Security scanning:** Use tools like Trivy to scan the Docker image

## License

Brocolli is licensed under the MIT License. See the LICENSE file for details.
