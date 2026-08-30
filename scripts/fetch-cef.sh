#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
. "$repo_dir/cef/manifest.env"

deps_dir=${CEF_DEPS_DIR:-"$repo_dir/deps"}
downloads_dir="$deps_dir/downloads"
target_dir="$deps_dir/$CEF_DISTRIBUTION"
archive=${CEF_ARCHIVE:-"$downloads_dir/$CEF_FILENAME"}

if [ -f "$target_dir/.browser-cef-version" ] &&
  [ "$(sed -n '1p' "$target_dir/.browser-cef-version")" = "$CEF_VERSION" ]; then
  printf '%s\n' "$target_dir"
  exit 0
fi

mkdir -p "$downloads_dir"
if [ ! -f "$archive" ]; then
  curl -fL "$CEF_URL" -o "$archive"
fi

actual_sha256=$(sha256sum "$archive" | awk '{print $1}')
if [ "$actual_sha256" != "$CEF_SHA256" ]; then
  printf 'CEF SHA-256 mismatch for %s\nexpected: %s\nactual:   %s\n' \
    "$archive" "$CEF_SHA256" "$actual_sha256" >&2
  exit 1
fi

temporary_dir=$(mktemp -d "$deps_dir/.cef-extract.XXXXXX")
cleanup() {
  rm -rf -- "$temporary_dir"
}
trap cleanup EXIT HUP INT TERM

tar -xjf "$archive" -C "$temporary_dir"
extracted_dir="$temporary_dir/$CEF_DISTRIBUTION"
if [ ! -f "$extracted_dir/Release/libcef.so" ] ||
  [ ! -d "$extracted_dir/include" ] ||
  [ ! -d "$extracted_dir/Resources" ]; then
  printf 'CEF archive is missing required Linux minimal distribution files\n' >&2
  exit 1
fi

if [ -e "$target_dir" ]; then
  printf 'CEF target already exists but does not match the manifest: %s\n' \
    "$target_dir" >&2
  exit 1
fi
printf '%s\n' "$CEF_VERSION" > "$extracted_dir/.browser-cef-version"
mv "$extracted_dir" "$target_dir"
printf '%s\n' "$target_dir"
