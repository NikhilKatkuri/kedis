FROM alpine:3 AS build

RUN apk add --no-cache build-base

WORKDIR /src
COPY server/ .
RUN make build/kedis-server

FROM alpine:3

RUN apk add --no-cache netcat-openbsd \
    && addgroup -g 10001 -S kedis \
    && adduser -u 10001 -S -G kedis -H kedis \
    && mkdir /data \
    && chown kedis:kedis /data

WORKDIR /app
COPY --from=build /src/build/kedis-server .
COPY --from=build /src/commands ./commands

ENV XDG_DATA_HOME=/data
VOLUME /data

USER kedis
EXPOSE 7379

HEALTHCHECK --interval=10s --timeout=3s --retries=3 \
    CMD nc -z 127.0.0.1 7379

CMD ["./kedis-server"]