
#!/bin/bash
 dir=$1
for file in `ls $dir | grep .bin`;
do 
  echo $dir/$file
  cat $dir/$file >> $dir/${dir}_all.bin
done

python fart.py -d ${dir}/${dir}_dexfile.dex -i $dir/${dir}_all.bin
cp ${dir}/${dir}_dexfile.dex-new.dex .