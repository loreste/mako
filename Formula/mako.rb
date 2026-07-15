# Homebrew formula sketch for Mako (local tap — homebrew-core is EXTERNAL).
#
#   brew tap-new yours/mako
#   cp Formula/mako.rb "$(brew --repo yours/mako)/Formula/"
#   brew install --build-from-source yours/mako/mako
#
# homebrew-core: needs your GitHub org + tagged tarball + core PR (see docs/RELEASE.md).
# mako discovers headers at ../share/mako/runtime relative to the binary,
# or via MAKO_RUNTIME.
class Mako < Formula
  desc "Mako — systems/backend language (.mko → native via C)"
  homepage "https://github.com/loreste/mako"
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
    rt.install "runtime/third_party/README.md" => "third_party/README.md" if File.file?("runtime/third_party/README.md")
  end

  def caveats
    <<~EOS
      Runtime headers installed to:
        #{share}/mako/runtime
      Found automatically next to the binary, or:
        export MAKO_RUNTIME=#{share}/mako/runtime
    EOS
  end

  test do
    assert_match "mako", shell_output("#{bin}/mako --version")
    (testpath/"hello.mko").write <<~EOS
      fn main() {
          print("ok")
      }
    EOS
    system bin/"mako", "run", "hello.mko"
  end
end
