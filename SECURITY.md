# Security Policy

## Supported project scope

Thermalright Mini Display is a local Windows desktop application intended to control the integrated LCD on the Thermalright Burst Assassin 120 Vision profile documented in \`docs/PROTOCOL.md\`. It does not require an online account, remote service or network connection for its display function.

## Reporting a security issue

Please do not publish exploitable details in a public issue before the maintainer has had a reasonable opportunity to review them. Use GitHub's private vulnerability reporting feature for this repository when it is enabled. Otherwise, contact the repository owner through a private GitHub channel.

Include the affected version, Windows version, steps to reproduce, impact and any relevant logs. Remove personal paths, identifiers, access tokens and unrelated private data from diagnostic material.

## Safety notes

- Only communicate with the supported VID/PID and validated BA120 PM/SUB handshake.
- Do not send arbitrary HID packets to unknown hardware.
- Startup integration writes only the current user's Windows Run registry entry.
- The application reads selected media files locally and stores preferences under the current user's Local AppData folder.
- Do not run unofficial binaries from untrusted sources.
