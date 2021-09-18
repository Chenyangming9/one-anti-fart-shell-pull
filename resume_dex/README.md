# 简介

这里对hanbing大佬的fart.py进行了简单的修改，使得在原先脚本解析完smali基础上，还能生成修复codeitem的dex文件。并且也封装了个sh脚本，方便快速修复dex。fart.py的运行环境是python2。

# 使用介绍

1. fart中每个dump出的dex和codeitem的文件名称都是以dex字节size开头，将所有某一个dex的指令bin文件和dex文件放到同一目录(dex字节size开头所有文件一并放入也不影响运行，只要关键文件包含即可，详见fix_dex.sh)，目录以dex size命名；
2. 运行fix_dex.sh脚本，例如针对给出的例子，运行命令：

`cd */resume_dex`

`./fix_dex.sh 2594772`

fix_dex.sh中执行的操作是将某个dex的所有的codeitem文件合并，然后调用fart.py脚本。

# 注明

原始fart.py来自于 [https://github.com/hanbinglengyue/FART/blob/master/fart.py](https://github.com/hanbinglengyue/FART/blob/master/fart.py)

