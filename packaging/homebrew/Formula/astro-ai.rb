class AstroAi < Formula
  desc "Free desktop app and terminal agent for chat, code and agents, powered by a team of free AI models that answer as one."
  homepage "https://esoexe.github.io/astro-ai/"
  version "1.9.1"
  license any_of: ["MIT", "Apache-2.0", "GPL-3.0-only"]

  livecheck do
    url :stable
    strategy :github_latest
  end

  depends_on :linux

  on_arm do
    url "https://github.com/esoexe/astro-ai/releases/download/v1.9.1/astro-ai-clean-1.9.1-linux-arm64-portable-CLEAN.tar.gz"
    sha256 "03bb83ea2e500a39a7e0f28029db1bdd9d5e67171a3ea87cf83525aec5d6b162"
  end
  on_intel do
    url "https://github.com/esoexe/astro-ai/releases/download/v1.9.1/astro-ai-clean-1.9.1-linux-x64-portable-CLEAN.tar.gz"
    sha256 "4929c48ad0c75895ffd75cdcc9138d2486d7aa70a0b95c70381e69836e40bdb5"
  end

  def install
    libexec.install Dir["*"]
    bin.write_exec_script libexec/"astro"
  end

  test do
    assert_match version.to_s, shell_output("#{bin}/astro --version")
  end
end
