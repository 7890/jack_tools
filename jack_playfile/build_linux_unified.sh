#!/bin/bash

#gzip -9 jack_playfile_static && base64 jack_playfile_static.gz > static/jack_playfile_static_64.gz.b64

#structure of output:

#bin/sh header
#arch detection, decompress
#64bit binary base64 data   #starting at BIN_64_START_LINE
#32bit binary base64 data   #starting at BIN_32_START_LINE

BIN_32=static/jack_playfile_static_32.gz.b64
BIN_64=static/jack_playfile_static_64.gz.b64

BIN_32_LEN=`cat ${BIN_32} | wc -l`
BIN_64_LEN=`cat ${BIN_64} | wc -l`

BIN_64_START_LINE=50;
BIN_64_END_LINE=`echo "${BIN_64_START_LINE} + ${BIN_64_LEN} - 1" | bc`;
BIN_32_START_LINE=`echo "${BIN_64_END_LINE} + 1" | bc`;

#backticks and dollars are escaped with preceeding \
cat - << __EOF__
#!/bin/bash
#part of jack_playfile - https://github.com/7890/jack_tools.

#enclosed in this file are statically compiled variants of 
#jack_playfile for 32bit and 64bit GNU/Linux platforms.
#the host architecture is evaluated and the corresponding
#binary is decompressed to a temporary directory and then started 
#with given arguments. after execution, the temporary directory is removed,
#for a permanent install of the platform-spcific binary
#simply copy /tmp/random/jack_playfile to /usr/local/bin (or any other
#similar place)

BIN_64_END_LINE=$BIN_64_END_LINE
BIN_32_START_LINE=$BIN_32_START_LINE

BIN_32_LEN=$BIN_32_LEN
BIN_64_LEN=$BIN_64_LEN

me=\$( readlink -m \$( type -p \$0 ))      # Full path to script

tmpdir=\`mktemp -d\`
uname -a | grep "x86_64" 2>&1 >/dev/null; ret=\$?
if [ x"\$ret" = "x0" ]; then
        cat "\$me" | head -\${BIN_64_END_LINE} | tail -\${BIN_64_LEN} | base64 -d | gunzip > "\$tmpdir"/jack_playfile
fi
uname -a | grep "i[0-9]86" 2>&1 >/dev/null; ret=\$?
#32 bit binary starts at line 12915, last line 26453
if [ x"\$ret" = "x0" ]; then
        cat "\$me" | tail -\${BIN_32_LEN} | base64 -d | gunzip > "\$tmpdir"/jack_playfile
fi
chmod 755 "\$tmpdir"/jack_playfile && ret=\`"\$tmpdir"/jack_playfile \$@\`
rm -rf "\$tmpdir"
exit \$ret















#line 49, next line is 64bit binary
__EOF__

cat $BIN_64
cat $BIN_32

