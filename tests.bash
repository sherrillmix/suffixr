#!/bin/bash

set -e


tmpDir=$(mktemp -d)
fastqFile=$tmpDir/test.fastq
cat<< END >$fastqFile
@seq1
GATATATATA
+
((((((((((
@seq2
CTCTCTCTCT
+
((((((((((
@seq3
GGGGGGGGGG
+
((((((((((
END
refFile=$tmpDir/ref.fa

cat<< END >$refFile
>ref
AAAAAAAAAAAAAGAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAACTCTCTAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
GATATATATTAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAACTAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAGAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAGAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
CTCTCTCTCTAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAACTCTCTCTCTAAAAA
END


./treefind 2>/dev/null && { echo "Missing files did not fail"; exit 1; }
./treefind $refFile 2>/dev/null && { echo "Missing file did not fail"; exit 1; }
./treefind -h $refFile $fastqFile 2>/dev/null && { echo "-h did not fail"; exit 1; }
./treefind -z $refFile $fastqFile 2>/dev/null && { echo "Weird argument did not fail"; exit 1; }
./treefind -s 10 $refFile $fastqFile 2>/dev/null && { echo "Weird argument did not fail"; exit 1; }
./treefind -$'\05' $refFile $fastqFile 2>/dev/null && { echo "Weird argument did not fail"; exit 1; }
./treefind $refFile $fastqFile 2>/dev/null || { echo "Running with 2 args failed"; exit 1; }
./treefind $refFile $fastqFile -t4 2>/dev/null || { echo "Running with 4 cores failed"; exit 1; }
./treefind $refFile $fastqFile -m2 2>/dev/null || { echo "Running with 2 mismatch failed"; exit 1; }
./treefind $refFile $fastqFile -m2 -t4 2>/dev/null || { echo "Running with 2 mismatch and 4 thread failed"; exit 1; }

cmd="./treefind $refFile $fastqFile -o $tmpDir/test"
echo $cmd
$cmd 2>/dev/null || { echo "Running with output file set to /tmp failed"; exit 1; }
zcat $tmpDir/test_match.fastq.gz |grep seq1 >/dev/null || { echo "seq1 not found in match"; exit 1; }
zcat $tmpDir/test_match.fastq.gz |grep seq2 >/dev/null || { echo "seq2 not found in match"; exit 1; }
zcat $tmpDir/test_match.fastq.gz |grep seq3 >/dev/null && { echo "seq3 erroneously found in match"; exit 1; }
zcat $tmpDir/test_partial.fastq.gz |grep seq3 >/dev/null && { echo "seq3 erroneously found in partial"; exit 1; }

cmd="./treefind $refFile $fastqFile -o $tmpDir/test -m 0"
echo $cmd
$cmd || { echo "Running with mismatch 0 failed"; exit 1; }
zcat $tmpDir/test_match.fastq.gz |grep seq1 >/dev/null && { echo "seq1 erroneously found in match"; exit 1; }
zcat $tmpDir/test_match.fastq.gz |grep seq2 >/dev/null || { echo "seq2 not found in match"; exit 1; }
zcat $tmpDir/test_match.fastq.gz |grep seq3 >/dev/null && { echo "seq3 erroneously found in match"; exit 1; }
zcat $tmpDir/test_partial.fastq.gz |grep seq3 >/dev/null && { echo "seq3 erroneously found in partial"; exit 1; }


longFileName=$tmpDir/$(printf '0123456789%.0s' {1..101})
#probably can't copy because filename too long but that means safe
(cp $refFile $longFileName 2>/dev/null || ./treefind $longFileName $fastqFile 2>/dev/null) && { echo "Long file name did not fail"; exit 1; }
exit 0
