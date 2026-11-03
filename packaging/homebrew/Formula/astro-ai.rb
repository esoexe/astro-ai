class AstroAi < Formula
  desc "Free desktop app and terminal agent for chat, code and agents, powered by a team of free AI models that answer as one."
  homepage "https://esoexe.github.io/astro-ai/"
  version "1.9.4"
  license any_of: ["MIT", "Apache-2.0", "GPL-3.0-only"]

  livecheck do
    url :stable
    strategy :github_latest
  end

  depends_on :linux

  on_arm do
    url "https://github.com/esoexe/astro-ai/releases/download/v1.9.4/astro-ai-clean-1.9.4-linux-arm64-portable-CLEAN.tar.gz"
    sha256 "1633a5fdb40970f1806c51cb250a8306be949f5aa4f907ca53560fed79552689"
  end
  on_intel do
    url "https://github.com/esoexe/astro-ai/releases/download/v1.9.4/astro-ai-clean-1.9.4-linux-x64-portable-CLEAN.tar.gz"
    sha256 "6cc9e08a3c7704a25edba739b5515d0bc881325f90bf8e39b84256e75dbfa9ac"
  end

  def install
    libexec.install Dir["*"]
    bin.write_exec_script libexec/"astro"
  end

  test do
    assert_match version.to_s, shell_output("#{bin}/astro --version")
  end
end
