FROM alpine:3.19

RUN apk update && apk add --no-cache \
    gcc \
    musl-dev \
    make \
    bash

WORKDIR /app

# Copy source code and Makefile
COPY Makefile ./
COPY src/ ./src/
COPY scripts/ ./scripts/
COPY docker/entrypoint.sh ./entrypoint.sh

# Make entrypoint executable and compile binary
RUN chmod +x ./entrypoint.sh && make

ENTRYPOINT ["./entrypoint.sh"]
CMD ["test"]
