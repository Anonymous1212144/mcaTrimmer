# mcaTrimmer
Trims mca files to remove unwanted chunks

This multithreaded program will work on every file in its folder that is named like region files, trimming ones where there are chunks you want to keep and **deleting** ones with no chunks you want. So **keep a backup before running**. Where "trim" means it removes data for chunks that you do not want to keep and compact the rest, so the file will become smaller.

# Instructions
Put the executable into the folder with the region files and create another file (default `chunks.txt`) filled with chunk coordinates of chunks you want to keep, separated by whitespace, and run it. The coordinate are in x, z order. So e.g. if you want to keep the chunks (0,2) and (-3, -5) you can put them in as below.
```
0 2
-3 -5
```
Note that the program treates space, new line, etc all the same.

The program can also be run with up to 2 arguments denoting the number of threads (default 2) and the input file name (default `chunks.txt`), in that order. You do not need to provide both but if you want to provide file name you must also provide number of threads. So e.g. this executes it with 5 threads and takes `cooler_input_file_name.txt` as the file with the chunnks you want to keep.
```
./trimmer 5 cooler_input_file_name.txt
```
