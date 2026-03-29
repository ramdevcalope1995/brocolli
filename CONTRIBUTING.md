# Contributing to Brocolli

Thank you for your interest in contributing to Brocolli! We welcome contributions from the community, whether they're bug reports, feature requests, documentation improvements, or code contributions.

## Code of Conduct

Please be respectful and constructive in all interactions. We are committed to providing a welcoming and inclusive environment for all contributors.

## Getting Started

### Prerequisites

- Linux (Ubuntu 22.04+ recommended)
- C compiler (GCC 7+ or Clang 5+)
- `libseccomp-dev`, `sqlite3`, `libcurl4-openssl-dev`, `libssl-dev`
- Kore web framework
- Docker and Docker Compose (for containerized development)

### Setting Up Your Development Environment

1. **Fork the repository** on GitHub.

2. **Clone your fork:**
   ```bash
   git clone https://github.com/your-username/brocolli.git
   cd brocolli
   ```

3. **Create a development branch:**
   ```bash
   git checkout -b feature/your-feature-name
   ```

4. **Install dependencies:**
   ```bash
   # Ubuntu/Debian
   sudo apt-get install -y libseccomp-dev sqlite3 libcurl4-openssl-dev libssl-dev
   ```

5. **Build the project:**
   ```bash
   make clean
   make
   ```

6. **Run tests (if available):**
   ```bash
   make test
   ```

## Development Workflow

### Code Style

Brocolli follows the **Google C++ Style Guide** (adapted for C). Key points:

- Use 4-space indentation
- Keep lines under 100 characters
- Use meaningful variable and function names
- Add comments for complex logic
- Use `snake_case` for variables and functions
- Use `UPPER_CASE` for constants and macros

### Commit Messages

Write clear, descriptive commit messages:

```
[component] Brief description of the change

More detailed explanation if needed. Reference issue numbers with #123.

- Bullet points for multiple changes
- Keep commits focused and atomic
```

### Pull Requests

1. **Push your branch:**
   ```bash
   git push origin feature/your-feature-name
   ```

2. **Create a Pull Request** on GitHub with:
   - A clear title describing the change
   - A detailed description of what was changed and why
   - Reference to any related issues (#123)
   - Screenshots or logs if applicable

3. **Respond to feedback** and make requested changes.

## Areas for Contribution

### High Priority

- **CDP Integration:** Complete the Chrome DevTools Protocol (CDP) WebSocket client for full browser automation.
- **QuantClaw Bridge:** Implement full JSON RPC communication with the QuantClaw gateway.
- **Security Hardening:** Improve seccomp filter rules and add custom AppArmor profiles.
- **Testing:** Add unit tests and integration tests for all modules.

### Medium Priority

- **Documentation:** Improve API documentation, add tutorials, and create architecture diagrams.
- **Performance:** Profile and optimize hot paths, especially in the sandbox engine.
- **Error Handling:** Improve error messages and add better logging.

### Lower Priority

- **Examples:** Create example applications demonstrating Brocolli's capabilities.
- **Tooling:** Build CLI tools for managing sandboxes and jobs.
- **Integrations:** Add support for additional LLM providers or messaging platforms.

## Reporting Bugs

If you find a bug, please:

1. **Check existing issues** to avoid duplicates.
2. **Create a new issue** with:
   - A clear title
   - Steps to reproduce
   - Expected behavior vs. actual behavior
   - Your environment (OS, kernel version, etc.)
   - Relevant logs or error messages

## Requesting Features

For feature requests:

1. **Check existing issues** to see if it's already been requested.
2. **Create a new issue** with:
   - A clear title and description
   - Use cases and motivation
   - Proposed implementation (if you have ideas)
   - Any relevant references or inspiration

## Testing

Before submitting a PR, ensure:

1. **Code compiles without warnings:**
   ```bash
   make clean && make NOOPT=1 NOHTTP=0
   ```

2. **Tests pass:**
   ```bash
   make test
   ```

3. **Docker build succeeds:**
   ```bash
   docker build -t brocolli:test .
   ```

4. **Manual testing** of your changes.

## Documentation

When adding new features:

1. **Update README.md** if the feature is user-facing.
2. **Add code comments** for complex logic.
3. **Update DEPLOYMENT.md** if it affects deployment.
4. **Create examples** if the feature is significant.

## License

By contributing to Brocolli, you agree that your contributions will be licensed under the MIT License.

## Questions?

Feel free to:
- Open a GitHub discussion
- Create an issue with the `question` label
- Reach out to the maintainers

Thank you for contributing to Brocolli! 🥦
