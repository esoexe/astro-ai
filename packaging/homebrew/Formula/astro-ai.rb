class AstroAi < Formula
  desc "Free desktop app and terminal agent for chat, code and agents, powered by a team of free AI models that answer as one."
  homepage "https://esoexe.github.io/astro-ai/"
  version "1.9.8"
  license any_of: ["MIT", "Apache-2.0", "GPL-3.0-only"]

  livecheck do
    url :stable
    strategy :github_latest
  end

  depends_on :linux

  on_arm do
    url "https://github.com/esoexe/astro-ai/releases/download/v1.9.8/astro-ai-clean-1.9.8-linux-arm64-portable-CLEAN.tar.gz"
    sha256 "88c998834232afd5e88a63186dff06eeb3c49940724db12efe3349875c18e98b"
  end
  on_intel do
    url "https://github.com/esoexe/astro-ai/releases/download/v1.9.8/astro-ai-clean-1.9.8-linux-x64-portable-CLEAN.tar.gz"
    sha256 "ea004ef953b029356d7bc2cd7166f3ecca80dfe868a4d48016bd186dfa3860f0"
  end

  def install
    libexec.install Dir["*"]
    bin.write_exec_script libexec/"astro"
  end

  test do
    assert_match version.to_s, shell_output("#{bin}/astro --version")
  end
end
