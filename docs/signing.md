# Code signing

The release builds are **unsigned** until you add signing settings to the GitHub repository. Unsigned builds work,
but Windows SmartScreen shows "Windows protected your PC" (users click *More info → Run anyway*) and macOS
Gatekeeper blocks the app until it is opened with right-click → *Open*.

The Desktop workflow (`.github/workflows/desktop.yml`) signs automatically as soon as the settings below exist.
Nothing else needs to change: the checksums, `latest.json`, Scoop, Homebrew and winget all pick up the signed files
because `SHA256SUMS.txt` is computed after signing.

| Platform | Option | Cost | What you add on GitHub |
| --- | --- | --- | --- |
| Windows | **SignPath Foundation** (recommended) | free for open source | secret `SIGNPATH_API_TOKEN`, variable `SIGNPATH_ORGANIZATION_ID` |
| Windows | your own certificate as a .pfx | paid | secrets `WINDOWS_CERT_PFX_BASE64`, `WINDOWS_CERT_PASSWORD` |
| macOS | Apple Developer ID + notarization | Apple Developer Program, $99/year | secrets `MACOS_CERT_P12_BASE64`, `MACOS_CERT_PASSWORD`, `MACOS_SIGN_IDENTITY`, `APPLE_ID`, `APPLE_TEAM_ID`, `APPLE_APP_PASSWORD` |

Secrets: *Settings → Secrets and variables → Actions → Secrets → New repository secret*.
Variables (not secret): same page, *Variables* tab.

---

## Windows with SignPath Foundation (free)

SignPath Foundation gives open-source projects a free code-signing certificate. The certificate is issued to
*SignPath Foundation*, so Windows shows "SignPath Foundation" as the publisher. Each release is approved by you
on signpath.io before it is signed.

### 1. Check the conditions

From [signpath.org/terms](https://signpath.org/terms), short version:

- OSI-approved license with no commercial dual licensing. A.S.T.R.O AI is MIT OR Apache-2.0 OR GPL-3.0: OK.
- No proprietary components, no malware or unwanted programs, actively maintained, already released.
- Only your own code and builds are signed. The signed exes are all built from this repository:
  `AstroAI-Clean.exe`, `astro-server.exe`, `astro.exe` and the Setup exe.
- Third-party parts: the Setup exe carries Microsoft's WebView2 bootstrapper (signed by Microsoft,
  freely redistributable) so Windows can install the WebView2 runtime if it's missing. Mention it in the application.
  Libraries like curl, miniz and webview are open source and compiled in.
- Everyone on the team uses **multi-factor authentication** on GitHub and SignPath.
- Every signed file carries product name and version metadata. The build already writes
  ProductName `A.S.T.R.O AI` and ProductVersion = the release version into all four exes.
- A **Code signing policy** section appears on the home page and the download page (step 5).

### 2. Apply

Apply on [signpath.org](https://signpath.org/) (the *Apply* link). Use the repository URL
`https://github.com/esoexe/astro-ai` and the download page `https://esoexe.github.io/astro-ai/download.html`.
Approval is manual and can take a while.

### 3. Set up the SignPath project

After approval, on [app.signpath.io](https://app.signpath.io):

1. *Trusted Build Systems*: add **GitHub.com** and link it to the project. Install the
   [SignPath GitHub App](https://github.com/apps/signpath) on `esoexe/astro-ai`.
2. *Project* slug: `astro-ai` (any other slug works; then set the variable `SIGNPATH_PROJECT_SLUG`).
3. *Signing policy* slug: `release-signing` (else set `SIGNPATH_POLICY_SLUG`). Keep manual approval on.
4. *Artifact configuration*: paste this and make it the default:

```xml
<artifact-configuration xmlns="http://signpath.io/artifact-configuration/v1">
  <zip-file>
    <!-- the installer users download -->
    <pe-file path="AstroAI-${version}-windows-x64-Setup-CLEAN.exe" product-name="A.S.T.R.O AI" product-version="${version}">
      <authenticode-sign/>
    </pe-file>
    <!-- the portable zip: every exe inside is signed, then the zip is rebuilt -->
    <zip-file path="AstroAI-${version}-windows-x64-portable-CLEAN.zip">
      <pe-file-set>
        <include path="AstroAI-${version}-windows-x64-CLEAN/*.exe" max-matches="unbounded" product-name="A.S.T.R.O AI" product-version="${version}"/>
        <for-each>
          <authenticode-sign/>
        </for-each>
      </pe-file-set>
    </zip-file>
  </zip-file>
</artifact-configuration>
```

5. Create an API token for a user with *submitter* rights on the policy.

### 4. Add the settings on GitHub

- Secret `SIGNPATH_API_TOKEN`: the API token.
- Variable `SIGNPATH_ORGANIZATION_ID`: shown in SignPath under *Settings → Organization*.
- Optional variables `SIGNPATH_PROJECT_SLUG` and `SIGNPATH_POLICY_SLUG` if you didn't use the defaults above.

With these set, the next tag build:

1. uploads the Setup exe and the portable zip as a short-lived artifact (`windows-unsigned`, deleted after a day,
   never part of the release);
2. sends a signing request and **waits up to an hour for you to approve it** on signpath.io;
3. downloads the signed files, checks each signature with `osslsigncode verify` and continues with checksums
   and the release.

If the token or organization variable is missing, the step is skipped and the build stays unsigned. A rejected or
expired request fails the build, and no release is made.

### 5. Publish the code signing policy (required by SignPath Foundation)

Add this section to the website home page and download page (and the README) **once the first signed release
is out**. Before approval the text would be untrue.

> **Code signing policy**
>
> Free code signing provided by [SignPath.io](https://about.signpath.io/), certificate by [SignPath Foundation](https://signpath.org/).
>
> - Committers and reviewers: [esoexe](https://github.com/esoexe)
> - Approvers: [esoexe](https://github.com/esoexe)
>
> Privacy: A.S.T.R.O AI sends your messages only to the AI providers you use (the free platforms in One Brain,
> or the keys you add) and, when sync is on, to your own A.S.T.R.O AI server and Supabase project. The update check
> reads `latest.json` from the website and sends nothing about you. Problem reports are never sent automatically.
> See each provider's terms for how it handles prompts.

### What stays unsigned

The Setup exe contains the app files in a compressed payload that SignPath can't open. The **installer itself
is signed** (that's what SmartScreen checks when a file is downloaded), but the installed `AstroAI-Clean.exe`
inside it isn't. The portable zip has every exe signed. To also sign the installed files, the build would need to
sign them before packing the installer: two signing rounds per release.

---

## Windows with your own certificate (.pfx)

If you already have an Authenticode certificate as a `.pfx` file:

```sh
base64 -w0 cert.pfx    # paste the output into the secret WINDOWS_CERT_PFX_BASE64
```

and put its password in `WINDOWS_CERT_PASSWORD`. The workflow signs every `.exe` with `osslsigncode`, using
SHA-256 and a DigiCert timestamp. SignPath wins if both are configured.

Note: since June 2023, public certificate authorities only issue new code-signing keys on hardware tokens or
cloud HSMs, so a downloadable .pfx is rare. That's why SignPath is the recommended route.

---

## macOS: Developer ID and notarization

Needs a paid [Apple Developer Program](https://developer.apple.com/programs/) membership ($99/year).

1. In Xcode or on developer.apple.com, create a **Developer ID Application** certificate. Export it from
   Keychain Access as a `.p12` with a password.
2. Secrets:
   - `MACOS_CERT_P12_BASE64`: `base64 -i cert.p12 | pbcopy`
   - `MACOS_CERT_PASSWORD`: the .p12 password
   - `MACOS_SIGN_IDENTITY`: the full name, e.g. `Developer ID Application: Your Name (TEAMID1234)`
     (`security find-identity -v -p codesigning` lists it)
3. For notarization:
   - `APPLE_ID`: your Apple ID e-mail
   - `APPLE_TEAM_ID`: the 10-character team ID
   - `APPLE_APP_PASSWORD`: an app-specific password from [account.apple.com](https://account.apple.com)
     (*Sign-In and Security → App-Specific Passwords*)

The workflow then signs the engine, the terminal app, the app bundle (hardened runtime, microphone entitlement for
Live voice) and the disk image. It sends the DMG to Apple's notary service, staples the ticket and checks the
result with `spctl`.

---

## Checking a signed release

- Windows: right-click the exe → *Properties → Digital Signatures*, or `Get-AuthenticodeSignature .\AstroAI-*.exe`
  in PowerShell. SmartScreen reputation builds up over the first downloads of a newly signed publisher.
- macOS: `spctl -a -t open --context context:primary-signature -v AstroAI-*.dmg` and
  `codesign -dv --verbose=2 "/Applications/A.S.T.R.O AI Clean.app"`.
- Linux: the `.deb` and `.tar.gz` are not signed. `SHA256SUMS.txt` on the release lists every file's checksum,
  and the in-app updater and `astro update` refuse files that don't match.
