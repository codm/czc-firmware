#!/usr/bin/env python3

Import("env")
import os

print("Try to build HMTL gzip files")

try:
    os.mkdir('./src/webh')
except OSError as error:
    print(error)    
    
os.chdir('./tools/webfilesbuilder/')


cwd = os.getcwd()
print("{0}".format(cwd))         

os.environ["PIOENV"] = env["PIOENV"]

print("pioenv: {0}".format(os.environ["PIOENV"]))

env.Execute("npm install")

env.Execute("npx gulp")