**Requirements**

1. Java JRE (duh!)
2. Eclipse with CDT (tested on Luna): http://www.eclipse.org/downloads/packages/eclipse-ide-cc-developers/lunasr2
3. Eclipse SCons plugin: http://sconsolidator.com/  
**WARNING**: by default the damn SCons plugin uses 16 threads(!!). Go to *Window->Preferences->SCons->Build Settings* in Eclipse and make that SoB use only 4 jobs(threads) or whatever you feel confortable with. It will positively murder your system if you run with 16 threads/jobs.

**Getting Started**

After setting up Eclipse just do a File->New->Other...
Select: C/C++ / New SCons project from existing source
Point the importer to the folder where the SConstruct resides (the root foolder of your git workspace normally)

**Build**

Just hit build. And remember for God's sake to not let it run 16 threads! 

