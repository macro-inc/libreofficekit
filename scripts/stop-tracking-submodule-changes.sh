#!/bin/bash

# This prevents git from trying to track a massive amount of files, speeding up git status and other commands
find libreoffice-core/translations -type f -exec git update-index --assume-unchanged '{}' +
find libreoffice-core/dictionaries -type f -exec git update-index --assume-unchanged '{}' +
find libreoffice-core/helpcontent2 -type f -exec git update-index --assume-unchanged '{}' +
