#!/usr/bin/env bash
# Signing helpers for the Camelion++ plugin list.
#
#   linux/sign_pluginlist.sh genkey  <dir>        create an Ed25519 key pair in <dir>
#   linux/sign_pluginlist.sh verify  <nppPluginList.so> <public.pem>
#
# The public key goes into Camelion++ (cmake -DCAMELION_PLUGINLIST_PUBKEY=public.pem);
# the private key is only given to the CI that builds nppPluginList.so
# (repository secret PLUGINLIST_SIGNING_KEY). Never commit the private key.
set -euo pipefail
case "${1:-}" in
  genkey)
    dir="${2:?directory}"; mkdir -p "$dir"; umask 077
    openssl genpkey -algorithm ed25519 -out "$dir/pluginlist-ed25519-private.pem"
    openssl pkey -in "$dir/pluginlist-ed25519-private.pem" -pubout -out "$dir/pluginlist-ed25519-pub.pem"
    echo "private: $dir/pluginlist-ed25519-private.pem (keep secret)"
    echo "public : $dir/pluginlist-ed25519-pub.pem"
    ;;
  verify)
    so="${2:?nppPluginList.so}"; pub="${3:?public key}"
    tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
    objcopy -I elf64-little -O binary --only-section=.npp_pluginlist "$so" "$tmp/list.json"
    objcopy -I elf64-little -O binary --only-section=.npp_pluginlist_sig "$so" "$tmp/list.sig"
    # strip the terminating NUL appended after the JSON
    python3 -c "import sys;p=sys.argv[1];d=open(p,'rb').read().rstrip(b'\0');open(p,'wb').write(d)" "$tmp/list.json"
    openssl pkeyutl -verify -rawin -pubin -inkey "$pub" -in "$tmp/list.json" -sigfile "$tmp/list.sig"
    ;;
  *) sed -n '2,10p' "$0"; exit 1 ;;
esac
