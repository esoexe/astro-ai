cask "astro-ai" do
  version "1.8.1"
  sha256 "3f7dfae9c55fa89fdf0223f7caa26052c766fdeff867b63daf6aec3796ed81e4"

  url "https://github.com/esoexe/astro-ai/releases/download/v#{version}/AstroAI-#{version}-macos-universal-CLEAN.dmg"
  name "A.S.T.R.O AI"
  desc "Free desktop app and terminal agent for chat, code and agents, powered by a team of free AI models that answer as one."
  homepage "https://esoexe.github.io/astro-ai/"

  livecheck do
    url :url
    strategy :github_latest
  end

  app "A.S.T.R.O AI Clean.app"
  binary "#{appdir}/A.S.T.R.O AI Clean.app/Contents/Resources/astro"

  zap trash: "~/Library/Application Support/A.S.T.R.O AI Clean"

  caveats <<~EOS
    A.S.T.R.O AI is not notarized by Apple yet. If macOS blocks the first start,
    right-click the app and choose Open, or install with:
      brew install --cask --no-quarantine esoexe/tap/astro-ai
  EOS
end
