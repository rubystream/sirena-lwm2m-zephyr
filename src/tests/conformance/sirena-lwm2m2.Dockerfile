FROM node:24.22.0-slim

ARG SIRENA_SERVER_VERSION=0.5.0
ARG SIRENA_CORE_VERSION=0.7.0
ARG SIRENA_SERVER_SHASUM=51e7bae8b634ecec3f6694d2d7f054abcd347955
ARG SIRENA_SERVER_INTEGRITY=hwEGtcDPkX0/keGxUOGK5oPhXJvtMh0c+ftMfUMroyVqb0//7taUAm9nptk56QqtEEADtaGnXi5Yq0tWo8M1Ew==

RUN apt-get update \
    && apt-get install -y --no-install-recommends ca-certificates curl \
    && curl -fsSL -o /tmp/sirena-server.tgz \
       "https://registry.npmjs.org/@sirena-lwm2m/server/-/server-${SIRENA_SERVER_VERSION}.tgz" \
    && echo "${SIRENA_SERVER_SHASUM}  /tmp/sirena-server.tgz" | sha1sum -c - \
    && node -e 'const crypto=require("node:crypto"),fs=require("node:fs");const hash=crypto.createHash("sha512").update(fs.readFileSync("/tmp/sirena-server.tgz")).digest("base64");if(hash!==process.env.SIRENA_SERVER_INTEGRITY){throw new Error("the npm integrity of the tarball does not match: "+hash);}' \
    && npm install --global --omit=dev --no-audit --no-fund "@sirena-lwm2m/core@${SIRENA_CORE_VERSION}" \
    && npm install --global --omit=dev --no-audit --no-fund /tmp/sirena-server.tgz \
    && rm -f /tmp/sirena-server.tgz \
    && npm cache clean --force \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /opt/sirena

ENTRYPOINT ["sirena", "--store=memory", "--coap-port=5683", "--http-port=8080"]
