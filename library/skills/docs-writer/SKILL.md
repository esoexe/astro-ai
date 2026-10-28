---
name: docs-writer
description: Write or update READMEs, how-to guides, API references and code comments that match the real behaviour of the code. Use when asked to document a project, function, CLI or API.
---

# Docs writer

## Steps

1. **Read before writing.** Check the code, the CLI help, config files and existing docs. Every command and option you document must exist.
2. **Pick the document type** and keep to it:
   - README: what it is, why it is useful, install, quick start (copy-paste runnable), configuration, where to get help, license.
   - How-to guide: one goal, numbered steps, expected output, troubleshooting.
   - Reference: every option or endpoint with type, default and an example.
   - Code comments: why the code is this way, constraints and non-obvious decisions. Never restate what the line already says.
3. **Show, then explain.** Lead with a working example, then the detail.
4. **Test the examples.** Run the commands and code snippets when possible; mark any you could not run.

## Style

- Short sentences, active voice, second person ("Run", "You can").
- Real values in examples, not `foo`/`bar`, and never real secrets.
- Headings that match what readers search for.
