class AstroAi < Formula
  desc "Free desktop app and terminal agent for chat, code and agents, powered by a team of free AI models that answer as one."
  homepage "https://esoexe.github.io/astro-ai/"
  version "1.8.1"
  license any_of: ["MIT", "Apache-2.0", "GPL-3.0-only"]

  livecheck do
    url :stable
    strategy :github_latest
  end

  depends_on :linux

  on_arm do
    url "https://github.com/esoexe/astro-ai/releases/download/v1.8.1/astro-ai-clean-1.8.1-linux-arm64-portable-CLEAN.tar.gz"
    sha256 "98add36778e780686c0bb8de133b33672795272e144be5861806c70ccbc83c7a"
  end
  on_intel do
    url "https://github.com/esoexe/astro-ai/releases/download/v1.8.1/astro-ai-clean-1.8.1-linux-x64-portable-CLEAN.tar.gz"
    sha256 "87b451fb1bae9e400c40b588425aa9c80d9f0d463a5399cc07b47b211d63f7cb"
  end

  def install
    libexec.install Dir["*"]
    bin.write_exec_script libexec/"astro"
  end

  test do
    assert_match version.to_s, shell_output("#{bin}/astro --version")
  end
end
