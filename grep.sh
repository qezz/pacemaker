#!/bin/sh



if [ $# -eq 1 ]
then
	find . -name "*" -not -path "./.git/*"  -exec grep -nHE "$1" '{}' 2>/dev/null \; | sed -r "s/Binar.*//g" | sed -r "s/.*test10.*//g" | tr -s '\n' 
else
	echo "Error! No regular expression/"
fi

