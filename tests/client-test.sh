#!/bin/bash
# Tests the SMBClient against the test server:
# client-test.sh <smb-test binary> <smb-client-test binary>
SERVER=$1
CLIENT=$2
DIR=$(mktemp -d)
trap 'kill $PID 2>/dev/null; rm -rf "$DIR"' EXIT

mkdir -p "$DIR"/rw "$DIR"/ro "$DIR"/sd
echo "readonly" > "$DIR"/ro/file.txt

SMB_ROOT="$DIR" "$SERVER" > "$DIR"/server.log 2>&1 &
PID=$!
sleep 1

SMB_ROOT="$DIR" "$CLIENT"
RC=$?
if [ $RC != 0 ]; then
  echo "--- server log"; cat "$DIR"/server.log
fi
exit $RC
