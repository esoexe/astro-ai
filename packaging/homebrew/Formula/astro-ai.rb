class AstroAi < Formula
  desc "Free desktop app and terminal agent for chat, code and agents, powered by a team of free AI models that answer as one."
  homepage "https://esoexe.github.io/astro-ai/"
  version "1.9.2"
  license any_of: ["MIT", "Apache-2.0", "GPL-3.0-only"]

  livecheck do
    url :stable
    strategy :github_latest
  end

  depends_on :linux

  on_arm do
    url "https://github.com/esoexe/astro-ai/releases/download/v1.9.2/astro-ai-clean-1.9.2-linux-arm64-portable-CLEAN.tar.gz"
    sha256 "6d15906d836f5dd3e564487d59f675149cc7175d48e6d56d829664959b567eb7"
  end
  on_intel do
    url "https://github.com/esoexe/astro-ai/releases/download/v1.9.2/astro-ai-clean-1.9.2-linux-x64-portable-CLEAN.tar.gz"
    sha256 "2d87154a7c4ae45054b68eea7e63c317a401aedc697866eb26871ef4c04e1fae"
  end

  def install
    libexec.install Dir["*"]
    bin.write_exec_script libexec/"astro"
  end

  test do
    assert_match version.to_s, shell_output("#{bin}/astro --version")
  end
end
