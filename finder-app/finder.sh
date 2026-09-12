#!/bin/sh

if [ $# -ne 2 ] ; then
  echo "Should have 2 paramaters  filesdir and searchstr like: " $0 "filesdir searchstr"
  exit 1
 fi

 filesdir=$1
 searchstr=$2


#if [ ! -d $filesdir ]; then
#    echo The file $filesdir is not a directory, stop
#    exit 1
#fi

n_files=$(find "$filesdir" -type f  2>/dev/null | wc -l)
n_matches=$(grep -rl "$searchstr" "$filesdir" | wc -l)

echo The number of files are $n_files and the number of matching lines are $n_matches
