class AstroAi < Formula
  desc "Free desktop app and terminal agent for chat, code and agents, powered by a team of free AI models that answer as one."
  homepage "https://esoexe.github.io/astro-ai/"
  version "1.9.7"
  license any_of: ["MIT", "Apache-2.0", "GPL-3.0-only"]

  livecheck do
    url :stable
    strategy :github_latest
  end

  depends_on :linux

  on_arm do
    url "https://github.com/esoexe/astro-ai/releases/download/v1.9.7/astro-ai-clean-1.9.7-linux-arm64-portable-CLEAN.tar.gz"
    sha256 "52b2d8bca9e6b8e006bf764a6d0adcc72701182b522a56eaeb7cd697baa846f5"
  end
  on_intel do
    url "https://github.com/esoexe/astro-ai/releases/download/v1.9.7/astro-ai-clean-1.9.7-linux-x64-portable-CLEAN.tar.gz"
    sha256 "b7c2c0ccc06419b21520e53322475895507ed2a19f93a2e181b40dce2d098cda"
  end

  def install
    libexec.install Dir["*"]
    bin.write_exec_script libexec/"astro"
  end

  test do
    assert_match version.to_s, shell_output("#{bin}/astro --version")
  end
end
