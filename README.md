# JPortalExt

​JPortalExt is the extension of original [JPortal](https://github.com/JPortal-system/system) project.

It distinguishes between normal methods and jportal methods, which is the method you want to trace.

## Contents

​jdk12-06222165c35f:     the extended Openjdk12 where online information collection is added

​trace:                  used to enable Intel Processor Trace and dump PT/Sideband/JVMRuntime data

​decode:                 used to decode hardware tracing data

modifier:               used to enable/disable jportal methods

​README:                 this file

## Building on Ubuntu

This part describes how to build JPortal on Ubuntu. First you need to git clone JPortal and promote to top-level source directory.

### Build trace and dump

Build executables JPortalTrace in trace/.
```
cmake path-to-trace/
make
```

### Build Openjdk

​Before building extended openjdk, replace the path of JPortalTrace of openjdk source codes with path to your built executables, which is in JPortalEnable.cpp of ​jdk12-06222165c35f/

**Note: you should build a debug version of Openjdk.**

```
bash path-to-jdk/configure --enable-debug

make
```
### Build modifier
```
cmake path-to-modifier/
make
```
### Build decode
```
cmake path-to-decode/
make
```

## Example
Write a Java Source code as follows:

```
public class Test {
    jportal public static int add(int a, int b) {
        return a+b;
    }
    public static void main(String[] args) {
        int a = 1;
        int b = 1;
        System.out.println(add(a, b));
    }
}
```

Or use modifier to change mark class files directly.

To trace control flow execution of method add in class Test
```
path-to-built-of-jdk/bin/javac Test.java
sudo path-to-built-of-jdk/bin/java -XX:+JPortal Test
sudo path-to-built-of-decode/decode -c ./
```

Use JPortalMethod/JPortalMethodNoinline/JPortalMethodComp flag to trace method entry/exit.

JPortalMethod: Use Control flow tracing for jitted code;

JPortalMethodNoinline: Collect entry/exit for major method in jitted code only

JPortalMethodComp: Trace complete method entry/exit
