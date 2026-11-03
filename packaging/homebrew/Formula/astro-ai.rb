class AstroAi < Formula
  desc "Free desktop app and terminal agent for chat, code and agents, powered by a team of free AI models that answer as one."
  homepage "https://esoexe.github.io/astro-ai/"
  version "1.9.3"
  license any_of: ["MIT", "Apache-2.0", "GPL-3.0-only"]

  livecheck do
    url :stable
    strategy :github_latest
  end

  depends_on :linux

  on_arm do
    url "https://github.com/esoexe/astro-ai/releases/download/v1.9.3/astro-ai-clean-1.9.3-linux-arm64-portable-CLEAN.tar.gz"
    sha256 "ba3fdf02d59d69fe3d1a28b77df55cc1e7f66604a490603c09c1f6df90327d29"
  end
  on_intel do
    url "https://github.com/esoexe/astro-ai/releases/download/v1.9.3/astro-ai-clean-1.9.3-linux-x64-portable-CLEAN.tar.gz"
    sha256 "12a323db9c90b20dd35a00800b2a330445d3214e8660f5d970de3cceece532f6"
  end

  def install
    libexec.install Dir["*"]
    bin.write_exec_script libexec/"astro"
  end

  test do
    assert_match version.to_s, shell_output("#{bin}/astro --version")
  end
end
