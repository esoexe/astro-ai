class AstroAi < Formula
  desc "Free desktop app and terminal agent for chat, code and agents, powered by a team of free AI models that answer as one."
  homepage "https://esoexe.github.io/astro-ai/"
  url "https://github.com/esoexe/astro-ai/releases/download/v1.7.0/astro-ai-clean-1.7.0-linux-x64-portable-CLEAN.tar.gz"
  version "1.7.0"
  sha256 "8a978f1de2c1f6c43ead2ed01f091f6055c8377e9c51bb32f7ac1308832adbe8"
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
