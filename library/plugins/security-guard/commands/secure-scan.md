---
description: Run a defensive security review of the project or the given paths and report prioritized fixes.
---
Review $ARGUMENTS (or the whole project) defensively. Map the entry points, follow untrusted input to SQL, shell, file paths, HTML, deserializers and outbound URLs, check authentication and per-object authorization, search for hard-coded secrets (never print them in full), and run the ecosystem's dependency audit tool if it is installed. Report a findings table (severity, location, impact, fix) and the top three actions. Do not write exploits.
