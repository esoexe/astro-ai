#!/usr/bin/env node
// Writes the package-manager manifests for a release from the CLEAN downloads and their SHA-256 sums.
//   node tools/package_managers.js [version] [downloads-dir]
// Outputs (all public, no secrets):
//   bucket/astro-ai.json                                    Scoop (this repo is the bucket)
//   packaging/homebrew/Casks/astro-ai.rb                    Homebrew cask: the macOS app (+ astro command)
//   packaging/homebrew/Formula/astro-ai.rb                  Homebrew formula: Linux x64 (astro command + engine)
//   packaging/winget/manifests/e/esoexe/AstroAI/<v>/...     winget (submit to microsoft/winget-pkgs yourself)
'use strict';
const fs = require('fs'); const path = require('path'); const crypto = require('crypto');
const ROOT = path.join(__dirname, '..');
const site = JSON.parse(fs.readFileSync(path.join(ROOT, 'website', 'site.json'), 'utf8'));
const V = process.argv[2] || site.version;
const DL = process.argv[3] || path.join(ROOT, 'website', 'downloads');
const REPO = 'esoexe/astro-ai';
const REL = `https://github.com/${REPO}/releases/download/v${V}`;
const HOME = 'https://esoexe.github.io/astro-ai/';
const DESC = 'Free desktop app and terminal agent for chat, code and agents, powered by a team of free AI models that answer as one.';
const LICENSE = 'MIT OR Apache-2.0 OR GPL-3.0-only';

const files = {
  zip: `AstroAI-${V}-windows-x64-portable-CLEAN.zip`,
  setup: `AstroAI-${V}-windows-x64-Setup-CLEAN.exe`,
  dmg: `AstroAI-${V}-macos-universal-CLEAN.dmg`,
  tar: `astro-ai-clean-${V}-linux-x64-portable-CLEAN.tar.gz`,
};
function sha(name) {
  const p = path.join(DL, name);
  if (fs.existsSync(p)) return crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex');
  const sums = path.join(DL, 'SHA256SUMS.txt');
  if (fs.existsSync(sums)) { const line = fs.readFileSync(sums, 'utf8').split('\n').find((l) => l.trim().endsWith(`  ${name}`)); if (line) return line.split(/\s+/)[0]; }
  return null;
}
const hashes = Object.fromEntries(Object.entries(files).map(([k, f]) => [k, sha(f)]));
const missing = Object.entries(hashes).filter(([, h]) => !h).map(([k]) => files[k]);
if (missing.length) console.warn(`warning: no checksum for ${missing.join(', ')} (that manifest is skipped)`);
const write = (rel, text) => { const p = path.join(ROOT, rel); fs.mkdirSync(path.dirname(p), { recursive: true }); fs.writeFileSync(p, text); console.log('wrote', rel); };

// ---------- Scoop ----------
if (hashes.zip) {
  write('bucket/astro-ai.json', JSON.stringify({
    version: V,
    description: DESC,
    homepage: HOME,
    license: LICENSE,
    notes: ['Run "astro" in a terminal, or open A.S.T.R.O AI from the Start menu.', 'Free model tiers have daily limits; add your own free keys in Settings -> Providers for more.'],
    architecture: { '64bit': { url: `${REL}/${files.zip}`, hash: hashes.zip, extract_dir: `AstroAI-${V}-windows-x64-CLEAN` } },
    bin: 'astro.exe',
    shortcuts: [['AstroAI-Clean.exe', 'A.S.T.R.O AI']],
    checkver: { github: `https://github.com/${REPO}` },
    autoupdate: {
      architecture: { '64bit': { url: `https://github.com/${REPO}/releases/download/v$version/AstroAI-$version-windows-x64-portable-CLEAN.zip`, extract_dir: 'AstroAI-$version-windows-x64-CLEAN' } },
      hash: { url: '$baseurl/SHA256SUMS.txt' },
    },
  }, null, 4) + '\n');
}

// ---------- Homebrew ----------
if (hashes.dmg) {
  write('packaging/homebrew/Casks/astro-ai.rb', `cask "astro-ai" do
  version "${V}"
  sha256 "${hashes.dmg}"

  url "https://github.com/${REPO}/releases/download/v#{version}/AstroAI-#{version}-macos-universal-CLEAN.dmg"
  name "A.S.T.R.O AI"
  desc "${DESC.replace(/"/g, '\\"')}"
  homepage "${HOME}"

  livecheck do
    url :url
    strategy :github_latest
  end

  app "A.S.T.R.O AI Clean.app"
  binary "#{appdir}/A.S.T.R.O AI Clean.app/Contents/Resources/astro"

  zap trash: "~/Library/Application Support/A.S.T.R.O AI Clean"

  caveats <<~EOS
    A.S.T.R.O AI is not notarized by Apple yet. If macOS blocks the first start,
    right-click the app and choose Open, or install with:
      brew install --cask --no-quarantine esoexe/tap/astro-ai
  EOS
end
`);
}
if (hashes.tar) {
  write('packaging/homebrew/Formula/astro-ai.rb', `class AstroAi < Formula
  desc "${DESC.replace(/"/g, '\\"')}"
  homepage "${HOME}"
  url "https://github.com/${REPO}/releases/download/v${V}/${files.tar}"
  version "${V}"
  sha256 "${hashes.tar}"
  license any_of: ["MIT", "Apache-2.0", "GPL-3.0-only"]

  livecheck do
    url :stable
    strategy :github_latest
  end

  depends_on arch: :x86_64
  depends_on :linux

  def install
    libexec.install Dir["*"]
    bin.write_exec_script libexec/"astro"
  end

  test do
    assert_match version.to_s, shell_output("#{bin}/astro --version")
  end
end
`);
}

// ---------- winget ----------
if (hashes.setup) {
  const dir = `packaging/winget/manifests/e/esoexe/AstroAI/${V}`;
  const head = (type) => `# yaml-language-server: $schema=https://aka.ms/winget-manifest.${type}.1.9.0.schema.json\n`;
  write(`${dir}/esoexe.AstroAI.yaml`, `${head('version')}PackageIdentifier: esoexe.AstroAI\nPackageVersion: ${V}\nDefaultLocale: en-US\nManifestType: version\nManifestVersion: 1.9.0\n`);
  write(`${dir}/esoexe.AstroAI.installer.yaml`, `${head('installer')}PackageIdentifier: esoexe.AstroAI\nPackageVersion: ${V}\nInstallerType: exe\nScope: user\nInstallModes:\n  - interactive\n  - silent\nInstallerSwitches:\n  Silent: /S\n  SilentWithProgress: /S\n  InstallLocation: /D=<INSTALLPATH>\nUpgradeBehavior: install\nCommands:\n  - astro\nInstallers:\n  - Architecture: x64\n    InstallerUrl: ${REL}/${files.setup}\n    InstallerSha256: ${hashes.setup.toUpperCase()}\nManifestType: installer\nManifestVersion: 1.9.0\n`);
  write(`${dir}/esoexe.AstroAI.locale.en-US.yaml`, `${head('defaultLocale')}PackageIdentifier: esoexe.AstroAI\nPackageVersion: ${V}\nPackageLocale: en-US\nPublisher: The Astro AI team\nPublisherUrl: https://github.com/esoexe\nPublisherSupportUrl: https://github.com/${REPO}/issues\nPackageName: A.S.T.R.O AI\nPackageUrl: ${HOME}\nLicense: ${LICENSE}\nLicenseUrl: https://github.com/${REPO}#license\nShortDescription: ${DESC}\nDescription: |-\n  A.S.T.R.O AI is a free desktop app and terminal agent. Many free AI models work together as one brain to answer,\n  write code and run agent jobs. Free model tiers have daily limits; you can add your own free keys for more.\nMoniker: astro-ai\nTags:\n  - ai\n  - assistant\n  - coding\n  - agent\n  - llm\n  - free\nReleaseNotesUrl: ${HOME}changelog.html\nManifestType: defaultLocale\nManifestVersion: 1.9.0\n`);
}
