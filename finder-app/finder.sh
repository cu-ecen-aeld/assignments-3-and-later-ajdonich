#!/bin/sh

# Set to 0/1 to disable/enable DEBUG logging 
DEBUG=0

# Validate command line
if [ $# -ne 2 ]; then
    echo "usage: finder.sh <path/to/directory> <expression>"
    exit 1
fi

DIRECTORY=${1}
EXPRESSION=${2}
if [ ! -d "${DIRECTORY}" ]; then
    echo "ERROR: ${DIRECTORY} is not a valid directory."
    exit 1
fi

# Find and count
fcount=0
lncount=0
for file in `find ${DIRECTORY}`; do
    if [ ! -d "${file}" ]; then
        fcount=$(($fcount+1))

        # Debug logging
        if [ ${DEBUG} -gt 0 ]; then
            lncnt=lncount
            echo "${fcount} ${file}" 
            grep ${EXPRESSION} ${file} | while read -r ln ; do
		lncnt=$(($lncnt+1))
                echo "  ${lncnt} ${ln}"
            done
        fi

        lncnt=`grep ${EXPRESSION} ${file} | wc -l`
        lncount=$(($lncount+$lncnt))
    fi
done

echo "The number of files are ${fcount} and the number of matching lines are ${lncount}"
exit 0
