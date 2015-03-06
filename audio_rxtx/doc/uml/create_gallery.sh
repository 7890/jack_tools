#!/bin/sh
dir="$1"
#prefix incl. trailing /
prefix="$2"
cur="`pwd`"
cd "$1"
echo '<html><body><div>'
ls *.png | while read line; do
	echo '<a href="'$prefix$line'"'' target="_blank"''><img src="'$prefix$line'" border="1"
style="padding-top:2px;padding-bottom:2px;padding-left:2px;padding-right:2px;
margin-top:5px;margin-bottom:5px;margin-left:5px;margin-right:5px;"></a><br>';
done
echo '</div></body></html>'
cd "$cur"
