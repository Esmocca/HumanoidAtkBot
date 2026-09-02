FROM ubuntu:24.04

RUN apt update && apt install -y g++ && rm -rf /var/lib/apt/lists/*

WORKDIR /root
COPY Server.cpp .

RUN g++ Server.cpp -o server

CMD ["./server"]
