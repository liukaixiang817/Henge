#!/bin/sh
# Served by the lab's local shared folder; execute only inside macOS.
result=/tmp/henge-rosetta-result.txt
: > "$result"
(
  while :; do
    /usr/bin/curl --connect-timeout 3 --max-time 5 --silent -T "$result" http://10.0.2.4/rosetta-progress.txt
    sleep 10
  done
) &
reporter=$!
trap 'kill "$reporter" 2>/dev/null || :' EXIT
{
  /usr/bin/sw_vers
  /usr/bin/uname -m
  mode=$(/usr/bin/curl --retry 5 --fail --silent http://10.0.2.4/engine-mode.txt)
  printf 'ENGINE_MODE=%s\n' "$mode"
  /usr/bin/curl --retry 5 --fail --silent http://10.0.2.4/arm_probe -o /tmp/henge-arm-probe
  /bin/chmod +x /tmp/henge-arm-probe
  /tmp/henge-arm-probe
  printf 'ARM_EXIT=%s\n' "$?"
  /usr/bin/curl --retry 5 --fail --silent http://10.0.2.4/rosetta_probe -o /tmp/henge-rosetta-probe
  /bin/chmod +x /tmp/henge-rosetta-probe
  printf 'STAGE=smoke\n'
  /tmp/henge-rosetta-probe
  probe=$?
  printf 'PROBE_EXIT=%s\n' "$probe"
  if [ "$mode" = 1 ]; then
    /usr/bin/curl --retry 5 --fail --silent http://10.0.2.4/rosetta_extended -o /tmp/henge-rosetta-extended
    /bin/chmod +x /tmp/henge-rosetta-extended
    printf 'STAGE=extended\n'
    /tmp/henge-rosetta-extended
    printf 'EXTENDED_EXIT=%s\n' "$?"
  elif [ "$mode" = 0 ] && [ "$probe" != 0 ]; then
    printf 'MAINLINE_INTEL_UNSUPPORTED=1\n'
  fi
} > "$result" 2>&1
/usr/bin/curl --retry 5 --retry-delay 5 --fail --silent --show-error -T "$result" http://10.0.2.4/rosetta-result.txt
