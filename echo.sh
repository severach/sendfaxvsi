#!/bin/sh
# Check to see if the file is still visible, show the options, then display stdin
ls /tmp/sendfaxvsi*
echo "$0" "$@"
cat -
