#!/bin/sh

FIL_DIR=$1
SEARCH_STR=$2

if [ -z ${FIL_DIR} ] || [ -z ${SEARCH_STR} ]
then 
	echo "too few arguments" 
	exit 1
fi

if [ ! -e ${FIL_DIR} ]
then
	echo "file NOT exists"
	exit 1
fi

NUM_OF_FILES=`grep -r -l $SEARCH_STR $FIL_DIR | wc -l`
NUM_OF_MACHING=`grep -r $SEARCH_STR $FIL_DIR | wc -l`


echo "The number of files are ${NUM_OF_FILES} and the number of matching lines are ${NUM_OF_MACHING}"

exit 0
