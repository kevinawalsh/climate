#!/bin/bash

dd="x"; hh="x"; tt="x";
while read d h m t; do
  if [ "$t" == "$tt" ]; then
    continue;
  fi;
  if [ "${h:0:1}" == "0" ]; then h=${h:1}; fi
  if [ "${m:0:}" == "0" ]; then m=${m:1}; fi
  if [ "$m" -gt 29 ]; then h=$((h+1)); fi;
  if [ "$h" -eq 24 ]; then h=0; fi
  if [ "$d" == "$dd" ]; then 
    d="";
    if [ "$h" == "$hh" ]; then 
      continue; 
    fi;
    hh="$h"
  else
    dd="$d";
    hh="$h";
  fi;
  echo "$d,$h,$t"
  tt="$t";
done
