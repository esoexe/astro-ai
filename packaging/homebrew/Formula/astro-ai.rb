class AstroAi < Formula
  desc "Free desktop app and terminal agent for chat, code and agents, powered by a team of free AI models that answer as one."
  homepage "https://esoexe.github.io/astro-ai/"
  version "2.0.0"
  license any_of: ["MIT", "Apache-2.0", "GPL-3.0-only"]

  livecheck do
    url :stable
    strategy :github_latest
  end

  depends_on :linux

  on_arm do
    url "https://github.com/esoexe/astro-ai/releases/download/v2.0.0/astro-ai-clean-2.0.0-linux-arm64-portable-CLEAN.tar.gz"
    sha256 "06531db6e21ec8cc9f27457b86c5baae652f11a1bb36a59e19a1033e8b544d3c"
  end
  on_intel do
    url "https://github.com/esoexe/astro-ai/releases/download/v2.0.0/astro-ai-clean-2.0.0-linux-x64-portable-CLEAN.tar.gz"
    sha256 "f7b4260260ae13cdc1ff18e0e116f75b17b1780219fe9d4d0f77d5ffa34b9135"
  end

  def install
    libexec.install Dir["*"]
    bin.write_exec_script libexec/"astro"
  end

  test do
    assert_match version.to_s, shell_output("#{bin}/astro --version")
  end
end
