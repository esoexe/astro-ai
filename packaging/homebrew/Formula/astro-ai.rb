class AstroAi < Formula
  desc "Free desktop app and terminal agent for chat, code and agents, powered by a team of free AI models that answer as one."
  homepage "https://esoexe.github.io/astro-ai/"
  version "1.9.6"
  license any_of: ["MIT", "Apache-2.0", "GPL-3.0-only"]

  livecheck do
    url :stable
    strategy :github_latest
  end

  depends_on :linux

  on_arm do
    url "https://github.com/esoexe/astro-ai/releases/download/v1.9.6/astro-ai-clean-1.9.6-linux-arm64-portable-CLEAN.tar.gz"
    sha256 "96f48dd512a756bcc3739a941e0397fe5fca3248d9b8841686d4de7aa6f5298a"
  end
  on_intel do
    url "https://github.com/esoexe/astro-ai/releases/download/v1.9.6/astro-ai-clean-1.9.6-linux-x64-portable-CLEAN.tar.gz"
    sha256 "f3f58b3a5c45b733dd44b845dd0e0e71337f6efdc8dd5cd9f4f4b9a1059fdc3a"
  end

  def install
    libexec.install Dir["*"]
    bin.write_exec_script libexec/"astro"
  end

  test do
    assert_match version.to_s, shell_output("#{bin}/astro --version")
  end
end
