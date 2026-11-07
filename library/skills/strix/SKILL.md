---
name: strix
description: Autonomous AI penetration testing and vulnerability analysis tool based on Strix (usestrix/strix). Scans codebases, APIs, and web apps for OWASP Top 10 vulnerabilities, validates exploits with proofs-of-concept, and generates remediation patches.
---

# Strix — Autonomous AI Penetration Testing & Vulnerability Assessment

Strix acts like an ethical hacker to dynamically analyze application architecture, discover real security vulnerabilities, validate them with concrete reproduction steps, and generate auto-fix patches.

## Key Capabilities

1. **Attack Surface Discovery & Reconnaissance**
   - Route mapping: identify all exposed endpoints, HTTP verbs, parameters, headers, and authentication requirements.
   - Technology stack fingerprinting: frameworks, ORMs, template engines, cryptographic libraries, middleware.
   - Asset inventory: public APIs, admin surfaces, debug endpoints, hidden files, exposed source maps.

2. **Vulnerability Detection (OWASP Top 10)**
   - **Broken Access Control & IDOR**: unauthorized resource access, privilege escalation, role confusion.
   - **Injection**: SQL/NoSQL injection, Command Injection, Template Injection (SSTI), XPath/LDAP injection.
   - **Cryptographic Failures**: hardcoded secrets, weak PRNG, insecure hashing, improper salt handling.
   - **Server-Side Request Forgery (SSRF)**: cloud metadata query attacks (`169.254.169.254`), internal network probes.
   - **Insecure Design & Business Logic**: race conditions, payment/coupon manipulation, state transition bypass.
   - **Security Misconfiguration**: permissive CORS headers (`Access-Control-Allow-Origin: *` with credentials), default passwords, open directories.
   - **Software and Data Integrity**: insecure deserialization, unverified package signatures.
   - **Cross-Site Scripting (XSS)**: DOM-based, stored, and reflected XSS in frontend applications.

3. **Proof-of-Concept (PoC) Validation & Remediation**
   - Deterministic PoC payload construction to verify exploitability without false positives.
   - Exact file:line root cause attribution.
   - Concrete, copy-paste ready remediation patches with automated regression tests.
