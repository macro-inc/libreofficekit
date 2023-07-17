FROM ubuntu:jammy

###
# IMPORTANT: Make sure you apply the following patches `./patches/convert-service-flag.patch` `./patches/convert-service-comments-in-margin.patch`
# before building the docker image
###
WORKDIR /

COPY ./libreoffice-core /lok
