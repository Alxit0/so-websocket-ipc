# Dockerfile: build and run the server
FROM debian:12-slim

# build deps
RUN apt-get update && apt-get install -y build-essential gcc make

WORKDIR /app

# copy source
COPY . /app

# build
RUN make all

EXPOSE 8080

# default command: read config from /app/server.conf
CMD ["./bin/concurrent-http-server", "server.conf"]
