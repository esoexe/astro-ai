cask "astro-ai" do
  version "1.9.4"
  sha256 "1fa1060ba78824d2aa7bf94d4ce87d9976b4b648d9b9922d4d4a9df3ad1fe8b6"

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
