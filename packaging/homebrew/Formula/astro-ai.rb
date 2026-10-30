class AstroAi < Formula
  desc "Free desktop app and terminal agent for chat, code and agents, powered by a team of free AI models that answer as one."
  homepage "https://esoexe.github.io/astro-ai/"
  version "1.8.0"
  license any_of: ["MIT", "Apache-2.0", "GPL-3.0-only"]

  livecheck do
    url :stable
    strategy :github_latest
  end

  depends_on :linux

  on_arm do
    url "https://github.com/esoexe/astro-ai/releases/download/v1.8.0/astro-ai-clean-1.8.0-linux-arm64-portable-CLEAN.tar.gz"
    sha256 "c0d9ba9b19737a94c855e5fd10b37f1f71a67c7683a66e6828e35472101a6e29"
  end
  on_intel do
    url "https://github.com/esoexe/astro-ai/releases/download/v1.8.0/astro-ai-clean-1.8.0-linux-x64-portable-CLEAN.tar.gz"
    sha256 "49f7760bea7bc2b8ebf7dfb5e889f30b08dc46e759bf9d74184db8f7b512c828"
  end

  def install
    libexec.install Dir["*"]
    bin.write_exec_script libexec/"astro"
  end

  test do
    assert_match version.to_s, shell_output("#{bin}/astro --version")
  end
end
