# Homebrew formula for leptris — builds the engine and its CLI from the
# GitHub release tarball cut by .github/workflows/release.yml (tag vX.Y.Z).
#
# HOW TO BUMP THE VERSION (edit `url` and `sha256` together — Homebrew
# derives the formula version from the URL, so no separate `version` line):
#
#   1. Pick the released tag  vX.Y.Z   (never an unreleased version).
#   2. Fetch the tarball and hash it:
#        curl -L "https://github.com/leptris/leptris/archive/refs/tags/vX.Y.Z.tar.gz" -o /tmp/leptris-X.Y.Z.tar.gz
#        shasum -a 256 /tmp/leptris-X.Y.Z.tar.gz
#   3. Set `url ".../vX.Y.Z.tar.gz"` and `sha256 "<digest>"`.
#
# Then validate locally:
#   ruby -c packaging/homebrew/leptris.rb
#   brew install --build-from-source packaging/homebrew/leptris.rb
#   brew audit --formula packaging/homebrew/leptris.rb
#
# This formula lives in-repo (Gate C5). Submission to homebrew-core is a
# separate, owner-approved step — see packaging/README.md.
class Leptris < Formula
  desc "Fast XML parser and XPath evaluator in pure C"
  homepage "https://github.com/leptris/leptris"
  url "https://github.com/leptris/leptris/archive/refs/tags/v1.9.379.tar.gz"
  sha256 "9e9d040647fc74ac3ca566dc24ca7002bd2287a8159bd1fc4a45feb112ca2c14"
  license "MIT"

  livecheck do
    url :stable
    regex(/^v?(\d+(?:\.\d+)+)$/i)
  end

  depends_on "cmake" => :build
  depends_on "utf8proc"

  # macOS ships iconv in /usr/lib; on Linux this maps to the Homebrew
  # libiconv formula (matches the CMake `find_package(Iconv REQUIRED)`
  # in the LEPTRIS_ENABLE_ICONV=ON path).
  uses_from_macos "libiconv"

  def install
    # Shared build so the bottle ships a dylib (the project gates its
    # library targets on LEPTRIS_BUILD_STATIC/LEPTRIS_BUILD_SHARED, not
    # CMake's BUILD_SHARED_LIBS). Tests/examples/man pages/benchmarks are
    # out of scope for the packaged artifact.
    system "cmake", "-B", "build", "-S", ".",
           "-DLEPTRIS_BUILD_SHARED=ON",
           "-DLEPTRIS_BUILD_STATIC=ON",
           "-DBUILD_TESTING=OFF",
           "-DBUILD_EXAMPLES=OFF",
           "-DLEPTRIS_BUILD_CLI=ON",
           "-DLEPTRIS_BUILD_BENCHMARKS=OFF",
           "-DLEPTRIS_BUILD_MAN_PAGES=OFF",
           "-DLEPTRIS_BUILD_DOCS=OFF",
           "-DLEPTRIS_ENABLE_UTF8PROC=ON",
           "-DLEPTRIS_ENABLE_ICONV=ON",
           *std_cmake_args
    system "cmake", "--build", "build"
    system "cmake", "--install", "build"
  end

  test do
    (testpath/"doc.xml").write <<~XML
      <?xml version="1.0" encoding="UTF-8"?>
      <catalog><book id="1">leptris</book></catalog>
    XML

    # CLI parses a real document from the installed prefix.
    output = shell_output("#{bin}/leptris parse #{testpath}/doc.xml")
    assert_match "leptris", output

    # The installed library answers an XPath query through the same CLI.
    output = shell_output("#{bin}/leptris xpath #{testpath}/doc.xml '/catalog/book/@id'")
    assert_match(/\bid="1"/, output)
  end
end
