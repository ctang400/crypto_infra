#!/bin/bash
echo $1
sed -i "s/’/'/g" $1
sed -i 's/“/"/g' $1
sed -i 's/”/"/g' $1
