---
description: Write release notes and a CHANGELOG entry from the commits since the last tag.
---
Find the previous tag (`git describe --tags --abbrev=0`) unless the range is given here: $ARGUMENTS. Read the commits in that range, drop changes with no user impact, and group the rest into Highlights, New, Improved, Fixed, Breaking changes (each with what to do) and Security. Match the existing CHANGELOG format if there is one, and show the entry before writing it to the file.
