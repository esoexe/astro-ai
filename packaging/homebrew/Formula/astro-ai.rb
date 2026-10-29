class AstroAi < Formula
  desc "Free desktop app and terminal agent for chat, code and agents, powered by a team of free AI models that answer as one."
  homepage "https://esoexe.github.io/astro-ai/"
  url "https://github.com/esoexe/astro-ai/releases/download/v1.6.1/astro-ai-clean-1.6.1-linux-x64-portable-CLEAN.tar.gz"
  version "1.6.1"
  sha256 "13180385c19756d55ae58bda12d8f411cfc65211bbdb26fb80660eb3256bd008"
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
