class AstroAi < Formula
  desc "Free desktop app and terminal agent for chat, code and agents, powered by a team of free AI models that answer as one."
  homepage "https://esoexe.github.io/astro-ai/"
  version "1.9.5"
  license any_of: ["MIT", "Apache-2.0", "GPL-3.0-only"]

  livecheck do
    url :stable
    strategy :github_latest
  end

  depends_on :linux

  on_arm do
    url "https://github.com/esoexe/astro-ai/releases/download/v1.9.5/astro-ai-clean-1.9.5-linux-arm64-portable-CLEAN.tar.gz"
    sha256 "1e3227b491add0aaef013e149a444151c105042fe0e5fc66a98584c01c799556"
  end
  on_intel do
    url "https://github.com/esoexe/astro-ai/releases/download/v1.9.5/astro-ai-clean-1.9.5-linux-x64-portable-CLEAN.tar.gz"
    sha256 "c9bed1a7074da96c064b817cce783e710c28e06fff21834fff9c3def75f44802"
  end

  def install
    libexec.install Dir["*"]
    bin.write_exec_script libexec/"astro"
  end

  test do
    assert_match version.to_s, shell_output("#{bin}/astro --version")
  end
end
