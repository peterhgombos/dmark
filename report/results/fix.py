#!/bin/python

import glob
import os
import re

if __name__ == "__main__":
    directory = "new"

    # Ensure folder for new files exists
    if not os.path.exists(directory):
        os.makedirs(directory)

    # Remove old gnuplot data files
    for f in glob.glob("new/*"):
        os.remove(f)

    filelist = []
    # Get list of all m5 data files
    for filestring in glob.glob("*.txt"):
       f = open(filestring)
       testrun = f.readline().rstrip()[2:]
       filelist.append((testrun, filestring))

    filelist = sorted(filelist, key=lambda x: x[0])
    for test, filename in filelist:
        print test
        f = open(filename)
        mylist = [i[0] for i in filter(None, [re.findall(r'.*speedup.*', line) for line in f])]
        fileending =  re.findall(r'^[^-]*', test)[0]

        for i in mylist:
            test, result = i.split(":")
            filepath = 'new/' + test

            filehandle = open(filepath + fileending, 'a+')
            filehandle.write(result + '\n')
            filehandle.close()
