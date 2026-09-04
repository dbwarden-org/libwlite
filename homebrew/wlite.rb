class Wlite < Formula
  desc "Tiny SQLite schema and migration toolkit"
  homepage "https://github.com/dbwarden-org/libwlite"
  url "https://github.com/dbwarden-org/libwlite/archive/refs/tags/v0.2.0.tar.gz"
  sha256 "PLACEHOLDER"
  license "MIT"

  depends_on "sqlite"

  def install
    system "make"
    system "make", "PREFIX=#{prefix}", "install"
  end

  test do
    assert_match version.to_s, shell_output("#{bin}/wlite version")
  end
end
