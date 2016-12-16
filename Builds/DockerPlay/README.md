# DockerPlay : what is this ?

As a lucky Debian Jessie users some of us are missing an impotant stack to play with rippled from the sources : gcc5. Instead of hacking our working station (and potentially break it) we can setup a rippled development environment thanks docker. 

Basic idea: being able to edit rippled source files with our favorite tools and then build, test and run rippled inside docker containers. 

# What do you need to play ?

* A Linux system with docker installed.
* A clone of this repo if not done already. 
  * We will define the path where you cloned it as RIPPLED_REPO_ROOT.
  * We will define DOCKER_PLAY_ROOT as RIPPLED_REPO_ROOT/Builds/DockerPlay.

# How to play ?


## Build Rippled from scratch

### Short bootstrap : 
```
DOCKER_PLAY_ROOT/rdde build

...

Install file: "build/gcc.release/rippled" as "build/rippled"
scons: done building targets.
```

### Behind the scene :

* Build a docker image called rippled-builder as defined in DOCKER_PLAY_ROOT/RippledBuilder/Dockerfile. 

**Also notice a rippled user is created to fit your host user uid/gid (Debian users generally have (uid,gid)=(1000,1000) but you're free to change the Dockerfile to fit your environment).**

If the rippled-builder image already exists skip this step.

* Run a container called rippled-builder with following volume: 
  * RIPPLED_REPO_ROOT:/RIPPLED:rw

**This setup allows you to reuse the same container to build rippled after any source file changes.**

If the container already exists then start it. The container will build rippled and stop.

* When done and if no error you will find your fresh rippled binary in RIPPLED_REPO_ROOT/build.



## Start your fresh Rippled

### Short bootstrap : 
```
DOCKER_PLAY_ROOT/rdde start [name - default: standalone]
43d4bd9a712c5e9fde6b914753e002ac444ee34bc59e4a819612482c8e767d
2016-Dec-16 18:09:32 JobQueue:NFO Auto-tuning to 6 validation/transaction/proposal threads
2016-Dec-16 18:09:32 Amendments:DBG Amendment C6970A8B603D8778783B61C0D445C23D1633CCFAEF0D43E7DBCD1521D34BD7C3 is supported.
2016-Dec-16 18:09:32 Amendments:DBG Amendment 4C97EBA926031A7CF7D7B36FDE3ED66DDA5421192D63DE53FFB46E43B9DC8373 is supported.
2016-Dec-16 18:09:32 Amendments:DBG Amendment C1B8D934087225F509BEB5A8EC24447854713EE447D277F69545ABFA0E0FD490 is supported.

...

```

### Behind the scene : 

* If no rippled file found in RIPPLED_REPO_ROOT/build directory exit.


* If you do not provide a name provided for your rippled instance default is "standalone". In such case rdde will :
  * look if DOCKER_PLAY_ROOT/RippledRunner/etc/standalone/rippled.cfg exists if not will copy DOCKER_PLAY_ROOT/RippledRunner/etc/standalone/rippled.tpl.cfg in to DOCKER_PLAY_ROOT/RippledRunner/etc/standalone/rippled.cfg.
  * look if DOCKER_PLAY_ROOT/RippledRunner/etc/standalone/validators.txt exists if not will copy RIPPLED_REPO_ROOT/doc/validators-example.txt to DOCKER_PLAY_ROOT/RippledRunner/etc/$NAME/validators.txt


* If you do provide a name for your rippled instance the script behavior will not change so much but you will have to provides configuration template yourself at least. In such case rdde script will : 

  * look if DOCKER_PLAY_ROOT/RippledRunner/etc/\[name\]/rippled.cfg exists. If not will copy DOCKER_PLAY_ROOT/RippledRunner/etc/\[name\]/rippled.tpl.cfg in to DOCKER_PLAY_ROOT/RippledRunner/etc/\[name\]/rippled.cfg. 

    **If DOCKER_PLAY_ROOT/RippledRunner/etc/\[name\]/rippled.tpl.cfg doesn't exist then script exit with error.**

  * look if DOCKER_PLAY_ROOT/RippledRunner/etc/\[name\]/validators.txt exists if not will copy  RIPPLED_REPO_ROOT/doc/validators-example.txt to DOCKER_PLAY_ROOT/RippledRunner/etc/\[name\]/validators.txt


* Build a docker image called rippled-runner based on DOCKER_PLAY_ROOT/RippledRunner/Dockerfile. 

**Also notice a rippled user is created to fit your host user uid/gid (Debian users generally have (uid,gid)=(1000,1000) but you're free to change the Dockerfile to fit your environment).**

If the rippled-runner image already exists skip this step.

* Run a container called rippled-[name - default: standalone] with following volume:
  * RIPPLED_REPO_ROOT/build/:/RIPPLED/bin:ro
  * DOCKER_PLAY_ROOT/RippledRunner/etc/\[name\]:/RIPPLED/etc:ro
  * DOCKER_PLAY_ROOT/RippledRunner/data/\[name\]:/RIPPLED/data:rw

**This setup allows you to reuse the same container to run rippled after any build.**

If the container already exists (and not started already) then start it. The container will start rippled with provided configuration.

* NOTE : following files/directories are ignored in rippled repo : 
  * rippled.cfg
  * validators.txt
  * Builds/DockerPlay/RippledRunner/data/*/db
  * Builds/DockerPlay/RippledRunner/data/*/log


### TODO :

* customize rippled starting parameters thanks docker environment variables.
* use docker-compose to setup multi rippled testing environment. 



## Stop your fresh Rippled

### Short bootstrap : 
```
DOCKER_PLAY_ROOT/rdde stop [name - default: standalone]
```

### Behind the scene : 
Simply stop rippled-\[name - default: standalone\] container. To remove the container use docker rm command...

## Test your fresh Rippled

### Short bootstrap : 
```
DOCKER_PLAY_ROOT/rdde test

...

56.0s, 127 suites, 589 cases, 333514 tests total, 0 failures

...

  190 passing (44s)
  27 pending
```

### Behind the scene : 

* If no rippled file found in RIPPLED_REPO_ROOT/build directory exit.

* Build a docker image called rippled-tester based on DOCKER_PLAY_ROOT/RippledTester/Dockerfile. 

**Also notice a rippled user is created to fit your host user uid/gid (Debian users generally have (uid,gid)=(1000,1000) but you're free to change the Dockerfile to fit your environment).**

If the rippled-tester image already exists skip this step.

* Run a container called rippled-tester with following volume
  * RIPPLED_REPO_ROOT:/RIPPLED:rw

**This setup allows you to reuse the same container to test rippled after any build.**

If the container already exists then start it. The container will install node dependencies modules if not done already and run rippled unit tests.

### TODO :

* customize unit test to run through docker environment variables.

