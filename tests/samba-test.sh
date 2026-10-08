#!/bin/bash
# Tests the SMBClient against Samba running in a Docker container:
# samba-test.sh <smb-client-test binary>
# Needs Docker and internet access (the alpine image installs samba)
CLIENT=$1
PORT=4451
NAME=smb-client-samba-test
DIR=$(mktemp -d)
trap 'docker rm -f $NAME > /dev/null 2>&1; rm -rf "$DIR"' EXIT

mkdir -p "$DIR"/test && chmod 777 "$DIR"/test
cat > "$DIR"/run.sh <<'SCRIPT'
#!/bin/sh
set -e
apk add --no-cache samba > /dev/null
adduser -D -H user
printf 'password\npassword\n' | smbpasswd -a -s user > /dev/null
chmod 777 /share
cat > /etc/samba/smb.conf <<CONF
[global]
  server role = standalone server
  server min protocol = SMB2_02
  map to guest = never
[test]
  path = /share
  read only = no
  valid users = user
CONF
echo "samba ready"
exec smbd --foreground --no-process-group --debug-stdout
SCRIPT
chmod +x "$DIR"/run.sh

docker rm -f $NAME > /dev/null 2>&1
docker run -d --name $NAME --dns 1.1.1.1 -p 127.0.0.1:$PORT:445 \
  -v "$DIR"/run.sh:/run.sh -v "$DIR"/test:/share alpine:latest /run.sh > /dev/null || exit 1

# wait until samba accepts connections
for i in $(seq 1 120); do
  docker logs $NAME 2>&1 | grep -q "smbd version" && break
  docker ps -q --filter name=$NAME | grep -q . || break
  sleep 2
done
sleep 1
if ! docker logs $NAME 2>&1 | grep -q "smbd version"; then
  echo "samba did not start"; docker logs $NAME; exit 1
fi
docker exec $NAME smbd --version

SMB_ROOT="$DIR" SMB_PORT=$PORT SMB_SHARE=test "$CLIENT"
