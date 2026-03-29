# Sandbox Skill

This skill provides isolated execution environments using Linux namespaces and seccomp.

## Tools

### sandbox_create
Create a new isolated sandbox environment.
- `net_enabled`: (boolean) Whether to enable network access.
- `mem_limit_mb`: (number) Memory limit in megabytes.
- `hostname`: (string) Optional custom hostname.

### sandbox_exec
Execute a command inside an existing sandbox.
- `id`: (string) The UUID of the sandbox.
- `path`: (string) The absolute path to the binary to execute.
- `args`: (array of strings) Arguments for the command.

### sandbox_destroy
Destroy a sandbox and release all resources.
- `id`: (string) The UUID of the sandbox to destroy.
