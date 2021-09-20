# one-anti-fart-shell-pull
对反fart壳的脱壳过程文件，记录于此，对应于看雪上一篇文章：[一次针对反fart的加壳apk的脱壳经历](https://bbs.pediy.com/thread-269329.htm)。修改后的fart代码，主要修改点如下：

1. 最主要的一点是能更全面地dump “\<clinit\>” 方法的codeitem；
2. 通过缩小脱壳范围，方便排查反fart的原因。

# 注明：

其中的fart.py、ActivityThread.java和art_method.cc内容都是修改于fart作者hanbing大神的github开源代码：[https://github.com/hanbinglengyue/FART](https://github.com/hanbinglengyue/FART) 。也感谢大神的开源分享，使得我们离脱壳不再那么遥远。

