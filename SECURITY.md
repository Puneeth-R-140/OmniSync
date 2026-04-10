# Security Policy

## Supported Versions

OmniSync uses semantic versioning for stable releases.

| Version | Supported |
|---------|-----------|
| 1.4.x   | Yes       |
| 1.3.x   | No        |
| < 1.3   | No        |

## Reporting a Vulnerability

Report vulnerabilities privately by opening a GitHub Security Advisory or by contacting the maintainer directly.

Please include:
- A clear description of the issue and impacted component.
- Reproduction steps or a proof of concept.
- Impact assessment (confidentiality, integrity, availability).
- Suggested mitigation if available.

## Response Process

- Acknowledge report within 72 hours.
- Provide a status update within 7 days.
- Coordinate fix and disclosure timeline with the reporter.
- Publish a patched release and release notes when validated.

## Scope

Security reports are in scope for:
- Core CRDT merge and serialization logic.
- Network encoding and transport helpers.
- Distributed GC coordination behavior.

Out of scope:
- Issues requiring local code execution in a trusted build environment.
- Denial-of-service risks caused by intentionally unbounded user workloads without resource limits.

## Hardening Guidance

For production deployments:
- Validate and bound all untrusted network payloads.
- Apply rate limits and authentication at the transport layer.
- Run fuzz and stability tests in CI before release.
- Track dependency/toolchain updates for security patches.
