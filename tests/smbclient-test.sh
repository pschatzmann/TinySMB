#!/bin/bash
# End to end test with smbclient: smbclient-test.sh <smb-test binary>
SERVER=$1
DIR=$(mktemp -d)
PORT=4450
FAILED=0
trap 'kill $PID 2>/dev/null; rm -rf "$DIR"' EXIT

mkdir -p "$DIR"/rw/sub "$DIR"/ro "$DIR"/sd "$DIR"/local
echo "hello" > "$DIR"/rw/hello.txt
echo "inner" > "$DIR"/rw/sub/inner.txt
echo "readonly" > "$DIR"/ro/file.txt
head -c 3000000 /dev/urandom > "$DIR"/local/big.bin

SMB_ROOT="$DIR" "$SERVER" > "$DIR"/server.log 2>&1 &
PID=$!
sleep 1

smb() {  # smb <share> <commands> [extra options]
  smbclient "//127.0.0.1/$1" -p $PORT -U user%password ${3} -c "$2" 2>&1
}
check() {  # check <name> <condition result>
  if [ "$2" = 0 ]; then echo "ok   $1"; else echo "FAIL $1"; FAILED=1; fi
}

smbclient -L //127.0.0.1 -p $PORT -U user%password 2>&1 | grep -q "rw *Disk"
check "share enumeration" $?
smb rw "ls" | grep -q "hello.txt"
check "list directory" $?
smb rw "cd sub; ls" | grep -q "inner.txt"
check "list sub directory" $?

for share in rw sd; do
  smb $share "lcd $DIR/local; put big.bin" > /dev/null
  cmp -s "$DIR"/local/big.bin "$DIR"/$share/big.bin
  check "$share: upload" $?
  smb $share "lcd $DIR/local; get big.bin back.bin" > /dev/null
  cmp -s "$DIR"/local/big.bin "$DIR"/local/back.bin
  check "$share: download" $?
  rm -f "$DIR"/local/back.bin
  smb $share "mkdir dir; rename big.bin dir/moved.bin" > /dev/null
  cmp -s "$DIR"/local/big.bin "$DIR"/$share/dir/moved.bin
  check "$share: mkdir and rename" $?
  smb $share "rmdir dir" | grep -q "NT_STATUS_DIRECTORY_NOT_EMPTY"
  check "$share: rmdir of non empty directory fails" $?
  smb $share "del dir/moved.bin; rmdir dir" > /dev/null
  [ ! -e "$DIR"/$share/dir ]
  check "$share: delete file and directory" $?
done

# fat: the files are in a disk image, so we check by downloading them
smb fat "lcd $DIR/local; put big.bin" > /dev/null
smb fat "lcd $DIR/local; get big.bin back.bin" > /dev/null
cmp -s "$DIR"/local/big.bin "$DIR"/local/back.bin
check "fat: upload and download" $?
rm -f "$DIR"/local/back.bin
smb fat "mkdir dir; rename big.bin dir/moved.bin; rename dir dir2" > /dev/null
smb fat "lcd $DIR/local; get dir2/moved.bin back.bin" > /dev/null
cmp -s "$DIR"/local/big.bin "$DIR"/local/back.bin
check "fat: mkdir, rename file and directory" $?
rm -f "$DIR"/local/back.bin
smb fat "rmdir dir2" | grep -q "NT_STATUS_DIRECTORY_NOT_EMPTY"
check "fat: rmdir of non empty directory fails" $?
smb fat "del dir2/moved.bin; rmdir dir2" > /dev/null
LIST=$(smb fat "ls")
echo "$LIST" | grep -q "blocks available" && ! echo "$LIST" | grep -q "dir2"
check "fat: delete file and directory" $?

smb ro "lcd $DIR/local; get file.txt" > /dev/null
cmp -s "$DIR"/ro/file.txt "$DIR"/local/file.txt
check "read-only: download" $?
smb ro "lcd $DIR/local; put big.bin" | grep -q "NT_STATUS_MEDIA_WRITE_PROTECTED"
check "read-only: upload fails" $?

reader() {  # reader <share> <commands>
  smbclient "//127.0.0.1/$1" -p $PORT -U reader%secret -c "$2" 2>&1
}
reader rw "lcd $DIR/local; get hello.txt" > /dev/null
cmp -s "$DIR"/rw/hello.txt "$DIR"/local/hello.txt
check "read-only user: download" $?
reader rw "lcd $DIR/local; put big.bin" | grep -q "NT_STATUS_MEDIA_WRITE_PROTECTED"
check "read-only user: upload fails" $?
reader rw "mkdir newdir" | grep -q "NT_STATUS_MEDIA_WRITE_PROTECTED"
check "read-only user: mkdir fails" $?
reader rw "rename hello.txt renamed.txt" > /dev/null
[ -e "$DIR"/rw/hello.txt ] && [ ! -e "$DIR"/rw/renamed.txt ]
check "read-only user: rename fails" $?
reader rw "del hello.txt" > /dev/null
[ -e "$DIR"/rw/hello.txt ]
check "read-only user: delete fails" $?

login() {  # login <user%password>: succeeds if the share can be listed
  smbclient //127.0.0.1/rw -p $PORT -U "$1" -c ls 2>&1 | grep -q "hello.txt"
}
control() {  # control <command>: executed by the test server
  echo "$1" > "$DIR"/control
  for i in $(seq 50); do [ -e "$DIR"/control ] || break; sleep 0.1; done
}
login temp%old
check "user change: initial password" $?
control "passwd temp new"
! login temp%old && login temp%new
check "user change: setPassword" $?
control "remove temp"
! login temp%new
check "user change: removeUser" $?

smbclient //127.0.0.1/rw -p $PORT -U user%wrong -c ls 2>&1 | grep -q "NT_STATUS_LOGON_FAILURE"
check "wrong password fails" $?
smbclient //127.0.0.1/rw -p $PORT -N -c ls 2>&1 | grep -q "NT_STATUS_LOGON_FAILURE"
check "guest login fails" $?
smb unknown "ls" | grep -q "NT_STATUS_BAD_NETWORK_NAME"
check "unknown share fails" $?
smb rw "ls" "--client-protection=sign" | grep -q "hello.txt"
check "signed session" $?
smb rw "ls" "-m SMB2_02" | grep -q "hello.txt"
check "dialect 2.0.2" $?

grep -q "timing /big.bin: .* reads" "$DIR"/server.log && grep -q "timing   storage" "$DIR"/server.log
check "timing log" $?

if [ $FAILED != 0 ]; then
  echo "--- server log"; cat "$DIR"/server.log
fi
exit $FAILED
