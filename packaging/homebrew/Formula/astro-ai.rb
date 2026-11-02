class AstroAi < Formula
  desc "Free desktop app and terminal agent for chat, code and agents, powered by a team of free AI models that answer as one."
  homepage "https://esoexe.github.io/astro-ai/"
  version "1.9.0"
  license any_of: ["MIT", "Apache-2.0", "GPL-3.0-only"]

  livecheck do
    url :stable
    strategy :github_latest
  end

  depends_on :linux

  on_arm do
    url "https://github.com/esoexe/astro-ai/releases/download/v1.9.0/astro-ai-clean-1.9.0-linux-arm64-portable-CLEAN.tar.gz"
    sha256 "5b1bcb85e611e739498d7d28061ddf0cba041d831d2d23992ee76fc3bfb1f9b1"
  end
  on_intel do
    url "https://github.com/esoexe/astro-ai/releases/download/v1.9.0/astro-ai-clean-1.9.0-linux-x64-portable-CLEAN.tar.gz"
    sha256 "338b3f5b57f6dd11abf8387fb1cadacb1e7d1fb99312aac0574134c2ee3dd6bb"
  end

  def install
    libexec.install Dir["*"]
    bin.write_exec_script libexec/"astro"
  end

  test do
    assert_match version.to_s, shell_output("#{bin}/astro --version")
  end
end
