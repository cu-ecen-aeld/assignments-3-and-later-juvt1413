#!/bin/sh

writefile=$1
writestr=$2

if [ -z ${writefile} ] || [ -z ${writestr} ]
then 
	echo "too few arguments" 
	exit 1
fi

mkdir -p $(dirname $writefile) 
echo ${writestr} > ${writefile}

exit 0
