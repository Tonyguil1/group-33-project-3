# COP4610 Project 3: FAT32 File System

## Group Members

- Name: Tony Guillen
  - FSUID: AAG22E
  - Contribution: Entire project



---

## Project Description

This project implements a user-space shell-like utility that can interpret and manipulate a FAT32 file system image. The program allows the user to navigate directories, create files and directories, open and close files, read and write file contents, move files or directories, and delete files or empty directories.

The program is designed to avoid corrupting the FAT32 image and to provide descriptive error messages when invalid commands or operations are entered.

---

## File Listing

```text
.
├── Makefile
├── README.md
├── include
│   ├── fat32.h
│   ├── shell.h
│   ├── directory.h
│   └── file.h
└── src
    ├── main.c
    ├── fat32.c
    ├── shell.c
    ├── directory.c
    └── file.c

---
## How to Run

First, compile the project from the root directory:

```bash
make

This creates an executable name filesys in the bin directory

to run the program, provide a FAT32 image file as a command-line argument

bash
./bin/filesys [FAT32_IMAGE]

Once the program starts, a shell prompt will appear:

fat32.img//>

From there, you can enter supported commands such as:

info
ls
cd DIRNAME
mkdir DIRNAME
creat FILENAME
open FILENAME -rw
write FILENAME "hello world"
read FILENAME 5
close FILENAME
rm FILENAME
rmdir DIRNAME
exit

to exit the program use

exit

to remove the compiled files run 

make clean
