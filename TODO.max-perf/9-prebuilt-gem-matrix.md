# Lever 9: prebuilt-gem platform matrix — parsanol-parity 12

Status: BLOCKED on the owner's go for the platform SET (releases
are the owner's call). Target (identical across leptris/yeptris/
parsanol): ruby, x86_64-linux, x86_64-linux-musl, aarch64-linux,
aarch64-linux-musl, arm-linux, arm-linux-musl, x86_64-darwin,
arm64-darwin, x64-mingw32, x64-mingw-ucrt, aarch64-mingw-ucrt.

Rationale (consumer-side): many Ruby users cannot compile; a
missing platform gem is a hard install failure. yeptris is
missing x86_64-linux-musl, aarch64-linux-musl, x64-mingw32 today.

Effort once approved: release.yml matrix edits + one release per
gem. Half a day.
