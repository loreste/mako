# Homebrew formula for Mako (private tap or local).
#
# Stable builds from the GitHub source tag (needs Rust at build time).
# Prebuilt binary install is also available via install-release.sh.
#
#   brew install --build-from-source Formula/mako.rb
#   # or private tap:
#   brew tap-new yours/mako
#   cp Formula/mako.rb "$(brew --repo yours/mako)/Formula/"
#   brew install --build-from-source yours/mako/mako
#
# homebrew-core: open a PR after `brew audit --strict --online mako`
# (external; requires maintainer review).
class Mako < Formula
  desc "Mako — systems/backend language (.mko → native via C)"
  homepage "https://github.com/loreste/mako"
  # After tagging v0.1.5: ./scripts/fill-release-packaging.sh v0.1.5
  url "https://github.com/loreste/mako/archive/refs/tags/v0.1.5.tar.gz"
  sha256 "0000000000000000000000000000000000000000000000000000000000000000"
  license "MIT"
  head "https://github.com/loreste/mako.git", branch: "main"

  depends_on "rust" => :build
  depends_on "openssl@3" => :optional
  depends_on "libnghttp2" => :optional
  depends_on "sqlite" => :optional

  def install
    system "cargo", "build", "--release"
    bin.install "target/release/mako"
    rt = share/"mako/runtime"
    rt.mkpath
    Dir["runtime/*.h"].each { |h| rt.install h }
    rt.install "runtime/certs" if File.directory?("runtime/certs")
    (rt/"third_party").mkpath
    if File.file?("runtime/third_party/README.md")
      rt.install "runtime/third_party/README.md" => "third_party/README.md"
    end
    if File.directory?("std")
      (share/"mako/std").mkpath
      (share/"mako/std").install Dir["std/*"]
    end
  end

  def caveats
    <<~EOS
      Runtime headers: #{share}/mako/runtime
      Stdlib:          #{share}/mako/std
      Auto-discovered next to the binary, or:
        export MAKO_RUNTIME=#{share}/mako/runtime
        export MAKO_STD=#{share}/mako/std
    EOS
  end

  test do
    assert_match "mako0.1.5", shell_output("#{bin}/mako --version")
    (testpath/"hello.mko").write <<~EOS
      fn main() {
          print("ok")
      }
    EOS
    system bin/"mako", "run", "hello.mko"
  end
end
