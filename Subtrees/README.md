# Subtrees

These directories come from entire outside repositories
brought in using git-subtree.

About git-subtree:

https://github.com/apenwarr/git-subtree <br>
http://blogs.atlassian.com/2013/05/alternatives-to-git-submodule-git-subtree/ <br>

## LevelDB

Ripple's fork of LevelDB is shared by the Bitcoin reference client project.

Repository <br>
```
git@github.com:ripple/LevelDB.git
```
Branch
```
ripple-fork
```

## LightningDB (a.k.a. MDB)

A supposedly fast memory-mapped key value database system

Repository <br>
```
git://gitorious.org/mdb/mdb.git
```
Branch
```
mdb.master
```

## websocket

Ripple's fork of websocketpp has some incompatible changes and Ripple specific includes.

Repository
```
git@github.com:ripple/websocketpp.git
```
Branch
```
ripple-fork
```

## protobuf

Ripple's fork of protobuf doesn't have any actual changes, but since the upstream
repository uses SVN, we have created a Git version to use with the git-subtree command.

Repository
```
git@github.com:ripple/protobuf.git
```
Branch
```
master
```

**NOTE** Linux builds use the protobuf installed in /usr/lib. This will be
fixed in a future revision.

## SQLite

Not technically a subtree but included here because it is a direct
copy of the official SQLite distributions available here:

http://sqlite.org/download.html
