FROM ubuntu:jammy

# IMPORTANT: Make sure you apply the `./patches/convert-service-flag.patch` before building the docker image
WORKDIR /

COPY ./libreoffice-core /lok
