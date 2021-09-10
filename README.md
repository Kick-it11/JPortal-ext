# JPortalExt

​JPortalExt is the extension of original [JPortal](https://github.com/JPortal-system/system) project. It distinguishes between normal methods and jportal methods.

## Contents

​jdk12-06222165c35f:     the extended Openjdk12 where online information collection is added

​trace:                  used to enable Intel Processor Trace

dump:                   used to dump JVM runtime infomation needed

​decode:                 used to decode hardware tracing data

modifier:               used to enable/disable jportal methods

​README:                 this file

## Building on Ubuntu

This part describes how to build JPortal on Ubuntu. First you need to git clone JPortal and promote to top-level source directory.

## Build trace and dump

Build executables for trace and dump separately.

### Build Openjdk

​Before building extended openjdk, replace the path of JPortalTracer && JPortalDumper of openjdk source codes with path to your built trace && dump executables.
**Note: you should build a debug version of Openjdk.**
